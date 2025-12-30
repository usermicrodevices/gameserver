"""
Ogre3D renderer for 3D game world
"""

import threading
import queue
import time
import math
import logging
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass

try:
    import ogre.renderer.OGRE as ogre
    import ogre.io.OIS as OIS
    OGRE_AVAILABLE = True
except ImportError:
    OGRE_AVAILABLE = False
    logging.warning("Ogre3D not available, using dummy renderer")

logger = logging.getLogger(__name__)


@dataclass
class EntityData:
    """Entity data for rendering"""
    entity_id: int
    entity_type: str
    position: Tuple[float, float, float]
    rotation: Tuple[float, float, float, float]
    scale: Tuple[float, float, float]
    mesh_name: str
    material_name: str
    visible: bool = True
    animation: Optional[str] = None
    animation_speed: float = 1.0


@dataclass
class ChunkData:
    """World chunk data"""
    chunk_x: int
    chunk_z: int
    terrain_data: List[List[float]]
    entities: List[EntityData]
    loaded: bool = False


class Ogre3DRenderer:
    """Ogre3D rendering engine"""

    def __init__(self, game_state, config):
        self.game_state = game_state
        self.config = config
        self.running = False

        # Ogre3D components
        self.root = None
        self.render_window = None
        self.scene_manager = None
        self.camera = None
        self.viewport = None
        self.overlay_system = None

        # Scene objects
        self.entities: Dict[int, ogre.Entity] = {}
        self.chunks: Dict[str, ChunkData] = {}
        self.player_node = None

        # Rendering queue
        self.render_queue = queue.Queue()

        # Camera settings
        self.camera_position = ogre.Vector3(0, 10, 20)
        self.camera_target = ogre.Vector3(0, 0, 0)
        self.camera_yaw = 0.0
        self.camera_pitch = 0.0

        # Input state
        self.input_manager = None
        self.keyboard = None
        self.mouse = None

        # Resources
        self.resource_group_manager = None

    def run(self):
        """Main render loop"""
        if not OGRE_AVAILABLE:
            logger.error("Ogre3D is not available")
            return

        try:
            self.initialize_ogre()
            self.load_resources()
            self.setup_scene()
            self.setup_input()

            self.running = True

            # Main render loop
            while self.running:
                self.process_render_queue()
                self.update_camera()
                self.root.renderOneFrame()
                time.sleep(1.0 / 60.0)  # 60 FPS

        except Exception as e:
            logger.error(f"Ogre3D renderer error: {e}")
            import traceback
            traceback.print_exc()
        finally:
            self.shutdown()

    def initialize_ogre(self):
        """Initialize Ogre3D engine"""
        # Create root
        self.root = ogre.Root(
            self.config['ogre']['plugins_path'],
            self.config['ogre']['resources_path'],
            self.config['ogre']['log_path']
        )

        # Setup resources
        self.resource_group_manager = ogre.ResourceGroupManager.getSingleton()
        self.setup_resources()

        # Configure render system
        render_systems = self.root.getAvailableRenderers()
        if not render_systems:
            raise RuntimeError("No Ogre render systems available")

        render_system = render_systems[0]
        self.root.setRenderSystem(render_system)

        # Create render window
        window_params = {
            'title': self.config['window']['title'],
            'width': self.config['window']['width'],
            'height': self.config['window']['height'],
            'fullScreen': self.config['client']['fullscreen'],
            'vsync': self.config['client']['vsync']
        }

        self.render_window = self.root.initialise(True, **window_params)

        # Create scene manager
        self.scene_manager = self.root.createSceneManager(
            ogre.ST_GENERIC, "MainSceneManager"
        )

        # Create camera
        self.camera = self.scene_manager.createCamera("MainCamera")
        self.camera.setNearClipDistance(0.1)
        self.camera.setFarClipDistance(self.config['client']['render_distance'])
        self.camera.setFOVy(ogre.Degree(self.config['client']['fov']))

        # Create viewport
        self.viewport = self.render_window.addViewport(self.camera)
        self.viewport.setBackgroundColour(ogre.ColourValue(0.3, 0.3, 0.4))

        # Setup overlay system
        self.overlay_system = ogre.OverlaySystem()
        self.scene_manager.addRenderQueueListener(self.overlay_system)

        # Set ambient light
        self.scene_manager.setAmbientLight(
            ogre.ColourValue(0.3, 0.3, 0.3)
        )

        # Create directional light
        directional_light = self.scene_manager.createLight("SunLight")
        directional_light.setType(ogre.Light.LT_DIRECTIONAL)
        directional_light.setDirection(ogre.Vector3(0.5, -1, 0.5))
        directional_light.setDiffuseColour(ogre.ColourValue(0.8, 0.8, 0.8))
        directional_light.setSpecularColour(ogre.ColourValue(0.5, 0.5, 0.5))

        logger.info("Ogre3D initialized successfully")

    def setup_resources(self):
        """Setup resource locations"""
        # Add resource paths
        self.resource_group_manager.addResourceLocation(
            "assets/models", "FileSystem", "General"
        )
        self.resource_group_manager.addResourceLocation(
            "assets/textures", "FileSystem", "General"
        )
        self.resource_group_manager.addResourceLocation(
            "assets/shaders", "FileSystem", "General"
        )

        # Load resources
        self.resource_group_manager.initialiseAllResourceGroups()

    def load_resources(self):
        """Load game resources"""
        # Preload common meshes
        meshes_to_preload = [
            "player.mesh",
            "npc.mesh",
            "tree.mesh",
            "rock.mesh",
            "building.mesh"
        ]

        for mesh in meshes_to_preload:
            try:
                ogre.MeshManager.getSingleton().load(mesh, "General")
            except:
                logger.warning(f"Could not load mesh: {mesh}")

    def setup_scene(self):
        """Setup initial scene"""
        # Create terrain
        self.create_terrain()

        # Create sky
        self.scene_manager.setSkyBox(
            True,
            "SkyBox",
            5000,
            True
        )

        # Create player node
        self.player_node = self.scene_manager.getRootSceneNode().createChildSceneNode(
            "PlayerNode",
            ogre.Vector3(0, 5, 0)
        )

        # Position camera behind player
        camera_node = self.player_node.createChildSceneNode(
            "CameraNode",
            ogre.Vector3(0, 2, 5)
        )
        camera_node.attachObject(self.camera)

        logger.info("Scene setup complete")

    def create_terrain(self):
        """Create terrain plane"""
        try:
            # Create manual object for terrain
            terrain = self.scene_manager.createManualObject("Terrain")
            terrain.begin("BaseWhite", ogre.RenderOperation.OT_TRIANGLE_LIST)

            # Create simple plane for now
            size = 100
            half_size = size / 2

            # Add vertices
            terrain.position(-half_size, 0, -half_size)
            terrain.position(half_size, 0, -half_size)
            terrain.position(-half_size, 0, half_size)
            terrain.position(half_size, 0, half_size)

            # Add triangles
            terrain.triangle(0, 1, 2)
            terrain.triangle(1, 3, 2)

            terrain.end()

            # Create scene node
            terrain_node = self.scene_manager.getRootSceneNode().createChildSceneNode()
            terrain_node.attachObject(terrain)

        except Exception as e:
            logger.error(f"Failed to create terrain: {e}")

    def setup_input(self):
        """Setup input handling"""
        # Note: This requires OIS (Object Oriented Input System)
        # For Kivy integration, we'll handle input through Kivy
        pass

    def process_render_queue(self):
        """Process render commands from queue"""
        try:
            while True:
                command = self.render_queue.get_nowait()
                self.execute_render_command(command)
        except queue.Empty:
            pass

    def execute_render_command(self, command):
        """Execute a render command"""
        cmd_type = command.get('type')

        if cmd_type == 'create_entity':
            self.create_entity(
                command['entity_id'],
                command['mesh'],
                command['position'],
                command['rotation'],
                command.get('scale', (1, 1, 1))
            )

        elif cmd_type == 'update_entity':
            self.update_entity(
                command['entity_id'],
                command.get('position'),
                command.get('rotation'),
                command.get('visible')
            )

        elif cmd_type == 'remove_entity':
            self.remove_entity(command['entity_id'])

        elif cmd_type == 'load_chunk':
            self.load_chunk(
                command['chunk_x'],
                command['chunk_z'],
                command.get('terrain_data')
            )

    def create_entity(self, entity_id, mesh_name, position, rotation, scale=(1, 1, 1)):
        """Create a new entity"""
        if entity_id in self.entities:
            self.remove_entity(entity_id)

        try:
            entity = self.scene_manager.createEntity(
                f"Entity_{entity_id}",
                mesh_name
            )

            entity_node = self.scene_manager.getRootSceneNode().createChildSceneNode(
                f"EntityNode_{entity_id}",
                ogre.Vector3(*position)
            )

            entity_node.attachObject(entity)
            entity_node.setScale(*scale)

            # Apply rotation (quaternion: x, y, z, w)
            if len(rotation) == 4:
                entity_node.setOrientation(ogre.Quaternion(*rotation))
            elif len(rotation) == 3:
                entity_node.yaw(ogre.Degree(rotation[1]))
                entity_node.pitch(ogre.Degree(rotation[0]))
                entity_node.roll(ogre.Degree(rotation[2]))

            self.entities[entity_id] = entity

        except Exception as e:
            logger.error(f"Failed to create entity {entity_id}: {e}")

    def update_entity(self, entity_id, position=None, rotation=None, visible=None):
        """Update entity properties"""
        node_name = f"EntityNode_{entity_id}"
        node = self.scene_manager.getSceneNode(node_name)

        if node:
            if position:
                node.setPosition(ogre.Vector3(*position))

            if rotation:
                if len(rotation) == 4:
                    node.setOrientation(ogre.Quaternion(*rotation))
                elif len(rotation) == 3:
                    node.setOrientation(ogre.Quaternion(
                        ogre.Degree(rotation[0]),
                        ogre.Vector3.UNIT_X
                    ))
                    node.yaw(ogre.Degree(rotation[1]))
                    node.roll(ogre.Degree(rotation[2]))

            if visible is not None:
                entity = self.entities.get(entity_id)
                if entity:
                    entity.setVisible(visible)

    def remove_entity(self, entity_id):
        """Remove entity from scene"""
        if entity_id in self.entities:
            try:
                entity = self.entities[entity_id]
                node = entity.getParentSceneNode()

                if node:
                    node.detachObject(entity)
                    self.scene_manager.destroySceneNode(node)

                self.scene_manager.destroyEntity(entity)
                del self.entities[entity_id]

            except Exception as e:
                logger.error(f"Failed to remove entity {entity_id}: {e}")

    def load_chunk(self, chunk_x, chunk_z, terrain_data=None):
        """Load a world chunk"""
        chunk_key = f"{chunk_x}_{chunk_z}"

        if chunk_key in self.chunks:
            return

        chunk = ChunkData(chunk_x, chunk_z, terrain_data or [], [])
        self.chunks[chunk_key] = chunk

        # Create chunk node
        chunk_node = self.scene_manager.getRootSceneNode().createChildSceneNode(
            f"Chunk_{chunk_x}_{chunk_z}",
            ogre.Vector3(chunk_x * 100, 0, chunk_z * 100)
        )

        # Create terrain for chunk
        if terrain_data:
            self.create_chunk_terrain(chunk_node, terrain_data)

        chunk.loaded = True
        logger.info(f"Loaded chunk {chunk_x},{chunk_z}")

    def create_chunk_terrain(self, parent_node, heightmap):
        """Create terrain from heightmap data"""
        try:
            terrain_manual = self.scene_manager.createManualObject(
                f"Terrain_{parent_node.getName()}"
            )

            terrain_manual.begin("TerrainMaterial", ogre.RenderOperation.OT_TRIANGLE_LIST)

            size = len(heightmap)
            scale = 1.0

            # Generate vertices
            for z in range(size):
                for x in range(size):
                    height = heightmap[z][x] if heightmap else 0
                    terrain_manual.position(x * scale, height, z * scale)
                    terrain_manual.textureCoord(x / (size-1), z / (size-1))
                    terrain_manual.normal(0, 1, 0)  # Simple normals

            # Generate indices
            for z in range(size-1):
                for x in range(size-1):
                    idx = z * size + x

                    # First triangle
                    terrain_manual.index(idx)
                    terrain_manual.index(idx + 1)
                    terrain_manual.index(idx + size)

                    # Second triangle
                    terrain_manual.index(idx + 1)
                    terrain_manual.index(idx + size + 1)
                    terrain_manual.index(idx + size)

            terrain_manual.end()

            parent_node.attachObject(terrain_manual)

        except Exception as e:
            logger.error(f"Failed to create chunk terrain: {e}")

    def update_camera(self):
        """Update camera position and orientation"""
        if self.player_node and self.camera:
            # Get camera node
            camera_node = self.camera.getParentSceneNode()

            if camera_node:
                # Smooth camera follow
                player_pos = self.player_node.getPosition()
                camera_target = camera_node.getPosition()

                # Interpolate for smooth movement
                smooth_factor = 0.1
                new_pos = camera_target + (player_pos - camera_target) * smooth_factor

                camera_node.setPosition(new_pos)

                # Look at player
                camera_node.lookAt(player_pos, ogre.Node.TS_WORLD)

    def sync_state(self, game_state):
        """Sync with game state updates"""
        # Update player position
        if game_state.player_position:
            self.render_queue.put({
                'type': 'update_entity',
                'entity_id': 0,  # Player ID
                'position': game_state.player_position
            })

        # Add/update entities
        for entity_id, entity_data in game_state.entities.items():
            if entity_id not in self.entities:
                self.render_queue.put({
                    'type': 'create_entity',
                    'entity_id': entity_id,
                    'mesh': entity_data.get('mesh', 'player.mesh'),
                    'position': entity_data.get('position', (0, 0, 0)),
                    'rotation': entity_data.get('rotation', (0, 0, 0, 1)),
                    'scale': entity_data.get('scale', (1, 1, 1))
                })
            else:
                self.render_queue.put({
                    'type': 'update_entity',
                    'entity_id': entity_id,
                    'position': entity_data.get('position'),
                    'rotation': entity_data.get('rotation')
                })

        # Remove entities
        for entity_id in list(self.entities.keys()):
            if entity_id not in game_state.entities and entity_id != 0:
                self.render_queue.put({
                    'type': 'remove_entity',
                    'entity_id': entity_id
                })

        # Load/unload chunks
        for chunk_key in game_state.loaded_chunks:
            chunk_x, chunk_z = map(int, chunk_key.split('_'))
            if chunk_key not in self.chunks:
                self.render_queue.put({
                    'type': 'load_chunk',
                    'chunk_x': chunk_x,
                    'chunk_z': chunk_z
                })

    def shutdown(self):
        """Clean shutdown of Ogre3D"""
        self.running = False

        if self.root:
            try:
                # Cleanup scene
                self.scene_manager.clearScene()

                # Destroy window
                if self.render_window:
                    self.render_window.destroy()

                # Destroy root
                self.root.shutdown()

            except Exception as e:
                logger.error(f"Error during Ogre3D shutdown: {e}")

        logger.info("Ogre3D renderer shut down")
