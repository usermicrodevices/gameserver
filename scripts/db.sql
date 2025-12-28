-- Player inventory table
CREATE TABLE player_inventory (
    player_id BIGINT PRIMARY KEY,
    inventory_data JSONB NOT NULL DEFAULT '{}',
    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Item templates table (shared across all shards as reference table)
CREATE TABLE item_templates (
    item_id VARCHAR(100) PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    description TEXT,
    item_type INTEGER NOT NULL,
    base_rarity INTEGER DEFAULT 0,
    max_stack_size INTEGER DEFAULT 1,
    level_requirement INTEGER DEFAULT 1,
    base_gold_value INTEGER DEFAULT 0,
    tradable BOOLEAN DEFAULT true,
    droppable BOOLEAN DEFAULT true,
    sellable BOOLEAN DEFAULT true,
    icon_texture VARCHAR(200),
    stats JSONB DEFAULT '{}',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Player items table (sharded by player_id)
CREATE TABLE player_items (
    player_id BIGINT,
    item_instance_id UUID DEFAULT gen_random_uuid(),
    template_id VARCHAR(100) NOT NULL,
    current_stats JSONB,
    durability FLOAT DEFAULT 100.0,
    socketed_items JSONB DEFAULT '[]',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (player_id, item_instance_id)
);

-- Loot tables configuration
CREATE TABLE loot_tables (
    table_id VARCHAR(100) PRIMARY KEY,
    table_name VARCHAR(100) NOT NULL,
    table_data JSONB NOT NULL,
    version INTEGER DEFAULT 1,
    last_modified TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Market/trading table
CREATE TABLE market_listings (
    listing_id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
    seller_id BIGINT NOT NULL,
    item_instance_id UUID NOT NULL,
    price_gold BIGINT NOT NULL,
    price_premium INTEGER DEFAULT 0,
    listing_duration INTEGER DEFAULT 86400, -- seconds
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP GENERATED ALWAYS AS (created_at + listing_duration * INTERVAL '1 second') STORED,
    status INTEGER DEFAULT 0 -- 0=active, 1=sold, 2=cancelled, 3=expired
);