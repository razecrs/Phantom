#include "field_dict.h"

/*
 * i built this dictionary from real field names i've seen across:
 * mobile games (Clash of Clans, Genshin, PUBG, Brawl Stars, FGO, AFK Arena,
 *               Marvel Snap, Pokemon GO, Coin Master, Clash Royale, Rise of Kingdoms,
 *               State of Survival, Lords Mobile, Game of Sultans, Garena FF,
 *               Mobile Legends, Wild Rift, Honkai, Wuthering Waves...)
 * apps         (Spotify, Netflix, YouTube Premium, Tinder, Duolingo, Headspace,
 *               Disney+, Crunchyroll, Audible, LinkedIn Premium, Canva Pro,
 *               Strava, Calm, NordVPN, ExpressVPN, Adobe, Notion, Todoist...)
 *
 * matching is case-insensitive substring on the JSON/proto field KEY.
 * one pattern can match many real fields:
 *   "currency" → softCurrency, hard_currency, premiumCurrency, currency_amount…
 */

const char *FIELD_CATEGORY_LABELS[FIELD_CATEGORY_COUNT] = {
    [FIELD_GAME_SOFT_CURRENCY] = "💰 Soft Currency",
    [FIELD_GAME_HARD_CURRENCY] = "💎 Hard Currency",
    [FIELD_GAME_ENERGY]        = "⚡ Energy / Lives",
    [FIELD_GAME_PROGRESSION]   = "📈 XP / Level / Rank",
    [FIELD_GAME_INVENTORY]     = "🪵 Crafting Materials",
    [FIELD_GAME_SOCIAL]        = "👥 Social / Fame",
    [FIELD_GAME_UNLOCK]        = "🔓 Unlock / Ownership",
    [FIELD_GAME_BATTLE]        = "🏆 Battle / League",
    [FIELD_GAME_SEASON]        = "🎫 Season / Battle Pass",
    [FIELD_GAME_GACHA]         = "🎰 Gacha / Summon",
    [FIELD_APP_SUBSCRIPTION]   = "⭐ Subscription / Plan",
    [FIELD_APP_FEATURE_FLAG]   = "🔧 Feature Flags",
    [FIELD_APP_CONTENT_GATE]   = "🚪 Content Access",
    [FIELD_APP_LIMITS]         = "📏 Usage Limits",
    [FIELD_APP_TRIAL]          = "⏳ Trial / Expiry",
    [FIELD_APP_ADS]            = "📺 Ads",
    [FIELD_APP_QUALITY]        = "🎧 Quality / Bitrate",
    [FIELD_APP_SOCIAL]         = "❤️  Likes / Followers",
    [FIELD_APP_COMMERCE]       = "🛒 Wallet / Balance",
};

/*
 * ─────────────────────────────────────────────────────────────────────────────
 * THE DICTIONARY
 * ─────────────────────────────────────────────────────────────────────────────
 * Format: { "pattern", CATEGORY, "UI label" }
 * patterns are lowercase — matcher does tolower() before compare
 */
const field_entry_t PHANTOM_FIELD_DICT[] = {

    /* ══════════════════════════════════════════════════════════════════
     * GAME — SOFT CURRENCY
     * farmable coins, basic progression currency, crafting gold
     * ══════════════════════════════════════════════════════════════════ */
    { "soft_currency",    FIELD_GAME_SOFT_CURRENCY, "Soft Currency"    },
    { "softcurrency",     FIELD_GAME_SOFT_CURRENCY, "Soft Currency"    },
    { "soft_coin",        FIELD_GAME_SOFT_CURRENCY, "Soft Coin"        },
    { "gold_amount",      FIELD_GAME_SOFT_CURRENCY, "Gold Amount"      },
    { "gold_count",       FIELD_GAME_SOFT_CURRENCY, "Gold Count"       },
    { "gold_balance",     FIELD_GAME_SOFT_CURRENCY, "Gold Balance"     },
    { "coin_amount",      FIELD_GAME_SOFT_CURRENCY, "Coin Amount"      },
    { "coin_count",       FIELD_GAME_SOFT_CURRENCY, "Coin Count"       },
    { "coin_balance",     FIELD_GAME_SOFT_CURRENCY, "Coin Balance"     },
    { "num_coins",        FIELD_GAME_SOFT_CURRENCY, "Num Coins"        },
    { "num_gold",         FIELD_GAME_SOFT_CURRENCY, "Num Gold"         },
    { "coins",            FIELD_GAME_SOFT_CURRENCY, "Coins"            },
    { "gold",             FIELD_GAME_SOFT_CURRENCY, "Gold"             },
    { "silver",           FIELD_GAME_SOFT_CURRENCY, "Silver"           },
    { "bronze",           FIELD_GAME_SOFT_CURRENCY, "Bronze"           },
    { "copper",           FIELD_GAME_SOFT_CURRENCY, "Copper"           },
    { "money",            FIELD_GAME_SOFT_CURRENCY, "Money"            },
    { "cash",             FIELD_GAME_SOFT_CURRENCY, "Cash"             },
    { "buck",             FIELD_GAME_SOFT_CURRENCY, "Bucks"            },
    { "elixir",           FIELD_GAME_SOFT_CURRENCY, "Elixir"           },
    { "dark_elixir",      FIELD_GAME_SOFT_CURRENCY, "Dark Elixir"      },
    { "darkelixir",       FIELD_GAME_SOFT_CURRENCY, "Dark Elixir"      },
    { "builder_gold",     FIELD_GAME_SOFT_CURRENCY, "Builder Gold"     },
    { "league_medal",     FIELD_GAME_SOFT_CURRENCY, "League Medal"     },
    { "war_loot",         FIELD_GAME_SOFT_CURRENCY, "War Loot"         },
    { "mora",             FIELD_GAME_SOFT_CURRENCY, "Mora"             },            /* Genshin */
    { "zenny",            FIELD_GAME_SOFT_CURRENCY, "Zenny"            },
    { "meseta",           FIELD_GAME_SOFT_CURRENCY, "Meseta"           },
    { "rupee",            FIELD_GAME_SOFT_CURRENCY, "Rupee"            },
    { "berry",            FIELD_GAME_SOFT_CURRENCY, "Berry"            },
    { "bell",             FIELD_GAME_SOFT_CURRENCY, "Bells"            },            /* Animal Crossing */
    { "poke_coin",        FIELD_GAME_SOFT_CURRENCY, "PokéCoins"        },
    { "pokecoin",         FIELD_GAME_SOFT_CURRENCY, "PokéCoins"        },
    { "stardust",         FIELD_GAME_SOFT_CURRENCY, "Stardust"         },
    { "candy",            FIELD_GAME_SOFT_CURRENCY, "Candy"            },
    { "mana_stone",       FIELD_GAME_SOFT_CURRENCY, "Mana Stone"       },
    { "hero_coin",        FIELD_GAME_SOFT_CURRENCY, "Hero Coin"        },
    { "guild_coin",       FIELD_GAME_SOFT_CURRENCY, "Guild Coin"       },
    { "arena_coin",       FIELD_GAME_SOFT_CURRENCY, "Arena Coin"       },
    { "friend_coin",      FIELD_GAME_SOFT_CURRENCY, "Friend Coin"      },
    { "event_coin",       FIELD_GAME_SOFT_CURRENCY, "Event Coin"       },
    { "honor_point",      FIELD_GAME_SOFT_CURRENCY, "Honor Points"     },
    { "loyalty_point",    FIELD_GAME_SOFT_CURRENCY, "Loyalty Points"   },
    { "reward_point",     FIELD_GAME_SOFT_CURRENCY, "Reward Points"    },
    { "activity_point",   FIELD_GAME_SOFT_CURRENCY, "Activity Points"  },
    { "contribution",     FIELD_GAME_SOFT_CURRENCY, "Contribution"     },

    /* ══════════════════════════════════════════════════════════════════
     * GAME — HARD CURRENCY
     * premium/paid currency — gems, diamonds, crystals, orbs
     * ══════════════════════════════════════════════════════════════════ */
    { "hard_currency",    FIELD_GAME_HARD_CURRENCY, "Hard Currency"    },
    { "hardcurrency",     FIELD_GAME_HARD_CURRENCY, "Hard Currency"    },
    { "premium_currency", FIELD_GAME_HARD_CURRENCY, "Premium Currency" },
    { "gem",              FIELD_GAME_HARD_CURRENCY, "Gems"             },
    { "diamond",          FIELD_GAME_HARD_CURRENCY, "Diamonds"         },
    { "crystal",          FIELD_GAME_HARD_CURRENCY, "Crystals"         },
    { "ruby",             FIELD_GAME_HARD_CURRENCY, "Rubies"           },
    { "emerald",          FIELD_GAME_HARD_CURRENCY, "Emeralds"         },
    { "sapphire",         FIELD_GAME_HARD_CURRENCY, "Sapphires"        },
    { "pearl",            FIELD_GAME_HARD_CURRENCY, "Pearls"           },
    { "amber",            FIELD_GAME_HARD_CURRENCY, "Amber"            },
    { "prism",            FIELD_GAME_HARD_CURRENCY, "Prism"            },
    { "jewel",            FIELD_GAME_HARD_CURRENCY, "Jewels"           },
    { "opal",             FIELD_GAME_HARD_CURRENCY, "Opals"            },
    { "amethyst",         FIELD_GAME_HARD_CURRENCY, "Amethyst"         },
    { "onyx",             FIELD_GAME_HARD_CURRENCY, "Onyx"             },
    { "topaz",            FIELD_GAME_HARD_CURRENCY, "Topaz"            },
    { "moonstone",        FIELD_GAME_HARD_CURRENCY, "Moonstone"        },
    { "runestone",        FIELD_GAME_HARD_CURRENCY, "Runestone"        },
    { "primogem",         FIELD_GAME_HARD_CURRENCY, "Primogems"        },           /* Genshin */
    { "astrite",          FIELD_GAME_HARD_CURRENCY, "Astrite"          },           /* Wuthering Waves */
    { "stellar_jade",     FIELD_GAME_HARD_CURRENCY, "Stellar Jade"     },           /* HSR */
    { "oneiro",           FIELD_GAME_HARD_CURRENCY, "Oneiric Shard"    },           /* HSR */
    { "chronos",          FIELD_GAME_HARD_CURRENCY, "Chrono Crystal"   },
    { "vip_gem",          FIELD_GAME_HARD_CURRENCY, "VIP Gems"         },
    { "paid_gem",         FIELD_GAME_HARD_CURRENCY, "Paid Gems"        },
    { "free_gem",         FIELD_GAME_HARD_CURRENCY, "Free Gems"        },
    { "topup",            FIELD_GAME_HARD_CURRENCY, "Top-Up Currency"  },
    { "recharge",         FIELD_GAME_HARD_CURRENCY, "Recharge"         },
    { "paid_diamond",     FIELD_GAME_HARD_CURRENCY, "Paid Diamonds"    },
    { "free_diamond",     FIELD_GAME_HARD_CURRENCY, "Free Diamonds"    },
    { "bp_gem",           FIELD_GAME_HARD_CURRENCY, "BP Gems"          },
    { "shard",            FIELD_GAME_HARD_CURRENCY, "Shards"           },
    { "fragment",         FIELD_GAME_HARD_CURRENCY, "Fragments"        },
    { "essence",          FIELD_GAME_HARD_CURRENCY, "Essence"          },
    { "soul",             FIELD_GAME_HARD_CURRENCY, "Souls"            },
    { "spirit",           FIELD_GAME_HARD_CURRENCY, "Spirits"          },
    { "orb",              FIELD_GAME_HARD_CURRENCY, "Orbs"             },
    { "rune",             FIELD_GAME_HARD_CURRENCY, "Runes"            },
    { "scroll",           FIELD_GAME_HARD_CURRENCY, "Scrolls"          },
    { "ingot",            FIELD_GAME_HARD_CURRENCY, "Ingots"           },
    { "aether",           FIELD_GAME_HARD_CURRENCY, "Aether"           },
    { "ether",            FIELD_GAME_HARD_CURRENCY, "Ether"            },
    { "flux",             FIELD_GAME_HARD_CURRENCY, "Flux"             },
    { "void_shard",       FIELD_GAME_HARD_CURRENCY, "Void Shards"      },

    /* ══════════════════════════════════════════════════════════════════
     * GAME — ENERGY / LIVES / MOVES
     * time-gated resources that refill automatically
     * ══════════════════════════════════════════════════════════════════ */
    { "energy",           FIELD_GAME_ENERGY, "Energy"            },
    { "stamina",          FIELD_GAME_ENERGY, "Stamina"           },
    { "lives",            FIELD_GAME_ENERGY, "Lives"             },
    { "heart",            FIELD_GAME_ENERGY, "Hearts"            },
    { "move",             FIELD_GAME_ENERGY, "Moves"             },
    { "turn",             FIELD_GAME_ENERGY, "Turns"             },
    { "action_point",     FIELD_GAME_ENERGY, "Action Points"     },
    { "action_pt",        FIELD_GAME_ENERGY, "Action Points"     },
    { "ap",               FIELD_GAME_ENERGY, "AP"                },
    { "mana",             FIELD_GAME_ENERGY, "Mana"              },
    { "fuel",             FIELD_GAME_ENERGY, "Fuel"              },
    { "power",            FIELD_GAME_ENERGY, "Power"             },
    { "charge",           FIELD_GAME_ENERGY, "Charges"           },
    { "attempt",          FIELD_GAME_ENERGY, "Attempts"          },
    { "play_count",       FIELD_GAME_ENERGY, "Play Count"        },
    { "raid_ticket",      FIELD_GAME_ENERGY, "Raid Tickets"      },
    { "arena_ticket",     FIELD_GAME_ENERGY, "Arena Tickets"     },
    { "dungeon_key",      FIELD_GAME_ENERGY, "Dungeon Keys"      },
    { "entry_key",        FIELD_GAME_ENERGY, "Entry Keys"        },
    { "refresh_count",    FIELD_GAME_ENERGY, "Refreshes"         },
    { "continue_count",   FIELD_GAME_ENERGY, "Continues"         },
    { "revive",           FIELD_GAME_ENERGY, "Revives"           },
    { "life_count",       FIELD_GAME_ENERGY, "Life Count"        },
    { "resin",            FIELD_GAME_ENERGY, "Resin"             },           /* Genshin/HSR */
    { "trailblaze_power", FIELD_GAME_ENERGY, "Trailblaze Power"  },           /* HSR */
    { "trailblaze",       FIELD_GAME_ENERGY, "Trailblaze Power"  },
    { "vigor",            FIELD_GAME_ENERGY, "Vigor"             },
    { "fatigue",          FIELD_GAME_ENERGY, "Fatigue"           },
    { "ap_max",           FIELD_GAME_ENERGY, "Max AP"            },
    { "max_energy",       FIELD_GAME_ENERGY, "Max Energy"        },
    { "cur_energy",       FIELD_GAME_ENERGY, "Current Energy"    },

    /* ══════════════════════════════════════════════════════════════════
     * GAME — PROGRESSION
     * experience, level, rank, score, mastery
     * ══════════════════════════════════════════════════════════════════ */
    { "exp",              FIELD_GAME_PROGRESSION, "EXP"              },
    { "xp",               FIELD_GAME_PROGRESSION, "XP"               },
    { "experience",       FIELD_GAME_PROGRESSION, "Experience"       },
    { "level",            FIELD_GAME_PROGRESSION, "Level"            },
    { "lv",               FIELD_GAME_PROGRESSION, "Level"            },
    { "lvl",              FIELD_GAME_PROGRESSION, "Level"            },
    { "rank",             FIELD_GAME_PROGRESSION, "Rank"             },
    { "tier",             FIELD_GAME_PROGRESSION, "Tier"             },
    { "grade",            FIELD_GAME_PROGRESSION, "Grade"            },
    { "star",             FIELD_GAME_PROGRESSION, "Stars"            },
    { "mastery",          FIELD_GAME_PROGRESSION, "Mastery"          },
    { "skill_point",      FIELD_GAME_PROGRESSION, "Skill Points"     },
    { "talent_point",     FIELD_GAME_PROGRESSION, "Talent Points"    },
    { "stat_point",       FIELD_GAME_PROGRESSION, "Stat Points"      },
    { "prestige",         FIELD_GAME_PROGRESSION, "Prestige"         },
    { "ascension",        FIELD_GAME_PROGRESSION, "Ascension"        },
    { "breakthrough",     FIELD_GAME_PROGRESSION, "Breakthrough"     },
    { "awakening",        FIELD_GAME_PROGRESSION, "Awakening"        },
    { "enhance",          FIELD_GAME_PROGRESSION, "Enhancement"      },
    { "refinement",       FIELD_GAME_PROGRESSION, "Refinement"       },
    { "intimacy",         FIELD_GAME_PROGRESSION, "Intimacy"         },
    { "friendship",       FIELD_GAME_PROGRESSION, "Friendship"       },
    { "bond",             FIELD_GAME_PROGRESSION, "Bond"             },
    { "affection",        FIELD_GAME_PROGRESSION, "Affection"        },
    { "trust",            FIELD_GAME_PROGRESSION, "Trust"            },
    { "reputation",       FIELD_GAME_PROGRESSION, "Reputation"       },
    { "mission_progress", FIELD_GAME_PROGRESSION, "Mission Progress" },
    { "quest_progress",   FIELD_GAME_PROGRESSION, "Quest Progress"   },
    { "chapter",          FIELD_GAME_PROGRESSION, "Chapter"          },
    { "stage",            FIELD_GAME_PROGRESSION, "Stage"            },
    { "floor",            FIELD_GAME_PROGRESSION, "Floor"            },
    { "wave",             FIELD_GAME_PROGRESSION, "Wave"             },

    /* ══════════════════════════════════════════════════════════════════
     * GAME — INVENTORY / CRAFTING MATERIALS
     * wood, ore, food, drops — anything that stacks in a backpack
     * ══════════════════════════════════════════════════════════════════ */
    { "wood",             FIELD_GAME_INVENTORY, "Wood"             },
    { "lumber",           FIELD_GAME_INVENTORY, "Lumber"           },
    { "stone",            FIELD_GAME_INVENTORY, "Stone"            },
    { "iron",             FIELD_GAME_INVENTORY, "Iron"             },
    { "steel",            FIELD_GAME_INVENTORY, "Steel"            },
    { "ore",              FIELD_GAME_INVENTORY, "Ore"              },
    { "coal",             FIELD_GAME_INVENTORY, "Coal"             },
    { "oil",              FIELD_GAME_INVENTORY, "Oil"              },
    { "food",             FIELD_GAME_INVENTORY, "Food"             },
    { "meat",             FIELD_GAME_INVENTORY, "Meat"             },
    { "grain",            FIELD_GAME_INVENTORY, "Grain"            },
    { "herb",             FIELD_GAME_INVENTORY, "Herbs"            },
    { "plant",            FIELD_GAME_INVENTORY, "Plants"           },
    { "fiber",            FIELD_GAME_INVENTORY, "Fiber"            },
    { "cloth",            FIELD_GAME_INVENTORY, "Cloth"            },
    { "leather",          FIELD_GAME_INVENTORY, "Leather"          },
    { "bone",             FIELD_GAME_INVENTORY, "Bones"            },
    { "feather",          FIELD_GAME_INVENTORY, "Feathers"         },
    { "scale",            FIELD_GAME_INVENTORY, "Scales"           },
    { "material",         FIELD_GAME_INVENTORY, "Materials"        },
    { "resource",         FIELD_GAME_INVENTORY, "Resources"        },
    { "supply",           FIELD_GAME_INVENTORY, "Supplies"         },
    { "component",        FIELD_GAME_INVENTORY, "Components"       },
    { "part",             FIELD_GAME_INVENTORY, "Parts"            },
    { "blueprint",        FIELD_GAME_INVENTORY, "Blueprints"       },
    { "recipe",           FIELD_GAME_INVENTORY, "Recipes"          },

    /* ══════════════════════════════════════════════════════════════════
     * GAME — SOCIAL / GUILD
     * ══════════════════════════════════════════════════════════════════ */
    { "power_level",      FIELD_GAME_SOCIAL, "Power Level"      },
    { "might",            FIELD_GAME_SOCIAL, "Might"            },
    { "combat_power",     FIELD_GAME_SOCIAL, "Combat Power"     },
    { "cp",               FIELD_GAME_SOCIAL, "Combat Power"     },
    { "bp",               FIELD_GAME_SOCIAL, "Battle Power"     },
    { "guild_exp",        FIELD_GAME_SOCIAL, "Guild EXP"        },
    { "guild_lv",         FIELD_GAME_SOCIAL, "Guild Level"      },
    { "fame",             FIELD_GAME_SOCIAL, "Fame"             },
    { "glory",            FIELD_GAME_SOCIAL, "Glory"            },
    { "honor",            FIELD_GAME_SOCIAL, "Honor"            },
    { "prestige_point",   FIELD_GAME_SOCIAL, "Prestige Points"  },

    /* ══════════════════════════════════════════════════════════════════
     * GAME — UNLOCK FLAGS
     * boolean or enum fields controlling access to content/heroes/items
     * ══════════════════════════════════════════════════════════════════ */
    { "is_locked",        FIELD_GAME_UNLOCK, "Is Locked"        },
    { "islocked",         FIELD_GAME_UNLOCK, "Is Locked"        },
    { "locked",           FIELD_GAME_UNLOCK, "Locked"           },
    { "is_unlocked",      FIELD_GAME_UNLOCK, "Is Unlocked"      },
    { "unlocked",         FIELD_GAME_UNLOCK, "Unlocked"         },
    { "is_owned",         FIELD_GAME_UNLOCK, "Is Owned"         },
    { "owned",            FIELD_GAME_UNLOCK, "Owned"            },
    { "is_purchased",     FIELD_GAME_UNLOCK, "Is Purchased"     },
    { "purchased",        FIELD_GAME_UNLOCK, "Purchased"        },
    { "is_available",     FIELD_GAME_UNLOCK, "Is Available"     },
    { "available",        FIELD_GAME_UNLOCK, "Available"        },
    { "is_enabled",       FIELD_GAME_UNLOCK, "Is Enabled"       },
    { "enabled",          FIELD_GAME_UNLOCK, "Enabled"          },
    { "is_active",        FIELD_GAME_UNLOCK, "Is Active"        },
    { "active",           FIELD_GAME_UNLOCK, "Active"           },
    { "is_completed",     FIELD_GAME_UNLOCK, "Is Completed"     },
    { "completed",        FIELD_GAME_UNLOCK, "Completed"        },
    { "is_cleared",       FIELD_GAME_UNLOCK, "Is Cleared"       },
    { "cleared",          FIELD_GAME_UNLOCK, "Cleared"          },
    { "hero_unlock",      FIELD_GAME_UNLOCK, "Hero Unlock"      },
    { "skin_unlock",      FIELD_GAME_UNLOCK, "Skin Unlock"      },
    { "chapter_unlock",   FIELD_GAME_UNLOCK, "Chapter Unlock"   },
    { "slot_unlock",      FIELD_GAME_UNLOCK, "Slot Unlock"      },
    { "feature_unlock",   FIELD_GAME_UNLOCK, "Feature Unlock"   },

    /* ══════════════════════════════════════════════════════════════════
     * GAME — BATTLE / COMPETITIVE
     * ══════════════════════════════════════════════════════════════════ */
    { "trophy",           FIELD_GAME_BATTLE, "Trophies"         },
    { "elo",              FIELD_GAME_BATTLE, "ELO"              },
    { "mmr",              FIELD_GAME_BATTLE, "MMR"              },
    { "rating",           FIELD_GAME_BATTLE, "Rating"           },
    { "league_point",     FIELD_GAME_BATTLE, "League Points"    },
    { "ranked_point",     FIELD_GAME_BATTLE, "Ranked Points"    },
    { "pvp_point",        FIELD_GAME_BATTLE, "PvP Points"       },
    { "win_streak",       FIELD_GAME_BATTLE, "Win Streak"       },
    { "win_count",        FIELD_GAME_BATTLE, "Win Count"        },
    { "kill",             FIELD_GAME_BATTLE, "Kills"            },
    { "kda",              FIELD_GAME_BATTLE, "KDA"              },
    { "match_count",      FIELD_GAME_BATTLE, "Match Count"      },

    /* ══════════════════════════════════════════════════════════════════
     * GAME — SEASON / BATTLE PASS
     * ══════════════════════════════════════════════════════════════════ */
    { "battle_pass",      FIELD_GAME_SEASON, "Battle Pass"      },
    { "battlepass",       FIELD_GAME_SEASON, "Battle Pass"      },
    { "season_pass",      FIELD_GAME_SEASON, "Season Pass"      },
    { "seasonpass",       FIELD_GAME_SEASON, "Season Pass"      },
    { "pass_level",       FIELD_GAME_SEASON, "Pass Level"       },
    { "bp_level",         FIELD_GAME_SEASON, "BP Level"         },
    { "bp_exp",           FIELD_GAME_SEASON, "BP EXP"           },
    { "season_level",     FIELD_GAME_SEASON, "Season Level"     },
    { "season_exp",       FIELD_GAME_SEASON, "Season EXP"       },
    { "is_bp_active",     FIELD_GAME_SEASON, "BP Active"        },
    { "has_pass",         FIELD_GAME_SEASON, "Has Pass"         },
    { "premium_pass",     FIELD_GAME_SEASON, "Premium Pass"     },
    { "season_ticket",    FIELD_GAME_SEASON, "Season Ticket"    },
    { "event_pass",       FIELD_GAME_SEASON, "Event Pass"       },
    { "pass_expire",      FIELD_GAME_SEASON, "Pass Expiry"      },

    /* ══════════════════════════════════════════════════════════════════
     * GAME — GACHA / SUMMON
     * ══════════════════════════════════════════════════════════════════ */
    { "pity",             FIELD_GAME_GACHA, "Pity Count"       },
    { "pity_count",       FIELD_GAME_GACHA, "Pity Count"       },
    { "soft_pity",        FIELD_GAME_GACHA, "Soft Pity"        },
    { "hard_pity",        FIELD_GAME_GACHA, "Hard Pity"        },
    { "pull",             FIELD_GAME_GACHA, "Pulls"            },
    { "summon",           FIELD_GAME_GACHA, "Summons"          },
    { "gacha",            FIELD_GAME_GACHA, "Gacha"            },
    { "draw",             FIELD_GAME_GACHA, "Draws"            },
    { "spin",             FIELD_GAME_GACHA, "Spins"            },
    { "roll",             FIELD_GAME_GACHA, "Rolls"            },
    { "wish",             FIELD_GAME_GACHA, "Wishes"           },           /* Genshin */
    { "convene",          FIELD_GAME_GACHA, "Convene"          },           /* WW */
    { "sigil",            FIELD_GAME_GACHA, "Sigils"           },
    { "summon_ticket",    FIELD_GAME_GACHA, "Summon Ticket"    },
    { "x10_ticket",       FIELD_GAME_GACHA, "10x Ticket"       },
    { "free_pull",        FIELD_GAME_GACHA, "Free Pulls"       },
    { "guaranteed",       FIELD_GAME_GACHA, "Guaranteed"       },
    { "banner_count",     FIELD_GAME_GACHA, "Banner Count"     },

    /* ══════════════════════════════════════════════════════════════════
     * APP — SUBSCRIPTION / PLAN
     * Spotify, Netflix, Tinder, Duolingo, etc.
     * ══════════════════════════════════════════════════════════════════ */
    { "is_premium",       FIELD_APP_SUBSCRIPTION, "Is Premium"       },
    { "ispremium",        FIELD_APP_SUBSCRIPTION, "Is Premium"       },
    { "premium",          FIELD_APP_SUBSCRIPTION, "Premium"          },
    { "is_subscribed",    FIELD_APP_SUBSCRIPTION, "Is Subscribed"    },
    { "issubscribed",     FIELD_APP_SUBSCRIPTION, "Is Subscribed"    },
    { "subscribed",       FIELD_APP_SUBSCRIPTION, "Subscribed"       },
    { "subscription",     FIELD_APP_SUBSCRIPTION, "Subscription"     },
    { "sub_type",         FIELD_APP_SUBSCRIPTION, "Sub Type"         },
    { "sub_status",       FIELD_APP_SUBSCRIPTION, "Sub Status"       },
    { "plan_type",        FIELD_APP_SUBSCRIPTION, "Plan Type"        },
    { "plan_name",        FIELD_APP_SUBSCRIPTION, "Plan Name"        },
    { "plan_id",          FIELD_APP_SUBSCRIPTION, "Plan ID"          },
    { "account_type",     FIELD_APP_SUBSCRIPTION, "Account Type"     },
    { "account_tier",     FIELD_APP_SUBSCRIPTION, "Account Tier"     },
    { "member_type",      FIELD_APP_SUBSCRIPTION, "Member Type"      },
    { "membership",       FIELD_APP_SUBSCRIPTION, "Membership"       },
    { "access_level",     FIELD_APP_SUBSCRIPTION, "Access Level"     },
    { "product_type",     FIELD_APP_SUBSCRIPTION, "Product Type"     },
    { "product_id",       FIELD_APP_SUBSCRIPTION, "Product ID"       },
    { "sku",              FIELD_APP_SUBSCRIPTION, "SKU"              },
    { "entitlement",      FIELD_APP_SUBSCRIPTION, "Entitlement"      },
    { "vip",              FIELD_APP_SUBSCRIPTION, "VIP"              },
    { "vip_level",        FIELD_APP_SUBSCRIPTION, "VIP Level"        },
    { "vip_type",         FIELD_APP_SUBSCRIPTION, "VIP Type"         },
    { "pro",              FIELD_APP_SUBSCRIPTION, "Pro"              },
    { "plus",             FIELD_APP_SUBSCRIPTION, "Plus"             },
    { "gold_member",      FIELD_APP_SUBSCRIPTION, "Gold Member"      },
    { "verified",         FIELD_APP_SUBSCRIPTION, "Verified"         },
    { "badge",            FIELD_APP_SUBSCRIPTION, "Badge"            },
    { "boost",            FIELD_APP_SUBSCRIPTION, "Boost"            },           /* Discord */
    { "nitro",            FIELD_APP_SUBSCRIPTION, "Nitro"            },           /* Discord */
    { "streak_freeze",    FIELD_APP_SUBSCRIPTION, "Streak Freeze"    },           /* Duolingo */
    { "super_",           FIELD_APP_SUBSCRIPTION, "Super"            },
    { "ultra_",           FIELD_APP_SUBSCRIPTION, "Ultra"            },

    /* ══════════════════════════════════════════════════════════════════
     * APP — FEATURE FLAGS
     * boolean toggles for premium features
     * ══════════════════════════════════════════════════════════════════ */
    { "can_download",     FIELD_APP_FEATURE_FLAG, "Can Download"     },
    { "can_offline",      FIELD_APP_FEATURE_FLAG, "Can Offline"      },
    { "offline_mode",     FIELD_APP_FEATURE_FLAG, "Offline Mode"     },
    { "can_skip",         FIELD_APP_FEATURE_FLAG, "Can Skip"         },
    { "can_play",         FIELD_APP_FEATURE_FLAG, "Can Play"         },
    { "can_stream",       FIELD_APP_FEATURE_FLAG, "Can Stream"       },
    { "shuffle_only",     FIELD_APP_FEATURE_FLAG, "Shuffle Only"     },
    { "is_shuffling",     FIELD_APP_FEATURE_FLAG, "Is Shuffling"     },
    { "high_quality",     FIELD_APP_FEATURE_FLAG, "High Quality"     },
    { "ultra_hd",         FIELD_APP_FEATURE_FLAG, "Ultra HD"         },
    { "hdr",              FIELD_APP_FEATURE_FLAG, "HDR"              },
    { "dolby",            FIELD_APP_FEATURE_FLAG, "Dolby"            },
    { "lossless",         FIELD_APP_FEATURE_FLAG, "Lossless"         },
    { "hifi",             FIELD_APP_FEATURE_FLAG, "HiFi"             },
    { "spatial_audio",    FIELD_APP_FEATURE_FLAG, "Spatial Audio"    },
    { "lyrics",           FIELD_APP_FEATURE_FLAG, "Lyrics"           },
    { "explicit",         FIELD_APP_FEATURE_FLAG, "Explicit Filter"  },
    { "connect",          FIELD_APP_FEATURE_FLAG, "Connect"          },           /* Spotify Connect */
    { "group_session",    FIELD_APP_FEATURE_FLAG, "Group Session"    },
    { "social_listening", FIELD_APP_FEATURE_FLAG, "Social Listening" },
    { "podcasts",         FIELD_APP_FEATURE_FLAG, "Podcasts"         },
    { "audiobook",        FIELD_APP_FEATURE_FLAG, "Audiobooks"       },
    { "reading",          FIELD_APP_FEATURE_FLAG, "Reading"          },
    { "ai_feature",       FIELD_APP_FEATURE_FLAG, "AI Features"      },
    { "dark_mode",        FIELD_APP_FEATURE_FLAG, "Dark Mode"        },
    { "custom_theme",     FIELD_APP_FEATURE_FLAG, "Custom Theme"     },
    { "widgets",          FIELD_APP_FEATURE_FLAG, "Widgets"          },
    { "early_access",     FIELD_APP_FEATURE_FLAG, "Early Access"     },
    { "beta",             FIELD_APP_FEATURE_FLAG, "Beta"             },
    { "advanced",         FIELD_APP_FEATURE_FLAG, "Advanced"         },
    { "pro_feature",      FIELD_APP_FEATURE_FLAG, "Pro Feature"      },
    { "export",           FIELD_APP_FEATURE_FLAG, "Export"           },
    { "import",           FIELD_APP_FEATURE_FLAG, "Import"           },
    { "collaboration",    FIELD_APP_FEATURE_FLAG, "Collaboration"    },
    { "workspace",        FIELD_APP_FEATURE_FLAG, "Workspace"        },
    { "unlimited",        FIELD_APP_FEATURE_FLAG, "Unlimited"        },

    /* ══════════════════════════════════════════════════════════════════
     * APP — CONTENT GATES
     * paywalled content, locked articles, restricted episodes
     * ══════════════════════════════════════════════════════════════════ */
    { "is_locked",        FIELD_APP_CONTENT_GATE, "Is Locked"        },
    { "paywalled",        FIELD_APP_CONTENT_GATE, "Paywalled"        },
    { "is_paywall",       FIELD_APP_CONTENT_GATE, "Is Paywalled"     },
    { "gated",            FIELD_APP_CONTENT_GATE, "Gated"            },
    { "restricted",       FIELD_APP_CONTENT_GATE, "Restricted"       },
    { "requires_sub",     FIELD_APP_CONTENT_GATE, "Requires Sub"     },
    { "premium_only",     FIELD_APP_CONTENT_GATE, "Premium Only"     },
    { "members_only",     FIELD_APP_CONTENT_GATE, "Members Only"     },
    { "exclusive",        FIELD_APP_CONTENT_GATE, "Exclusive"        },
    { "early_release",    FIELD_APP_CONTENT_GATE, "Early Release"    },
    { "preview",          FIELD_APP_CONTENT_GATE, "Preview"          },
    { "sample_only",      FIELD_APP_CONTENT_GATE, "Sample Only"      },
    { "watermark",        FIELD_APP_CONTENT_GATE, "Watermark"        },

    /* ══════════════════════════════════════════════════════════════════
     * APP — USAGE LIMITS
     * rate limits, daily caps, quota remaining
     * ══════════════════════════════════════════════════════════════════ */
    { "skip_limit",       FIELD_APP_LIMITS, "Skip Limit"        },
    { "skip_count",       FIELD_APP_LIMITS, "Skip Count"        },
    { "skips_left",       FIELD_APP_LIMITS, "Skips Left"        },
    { "skips_remaining",  FIELD_APP_LIMITS, "Skips Remaining"   },
    { "play_limit",       FIELD_APP_LIMITS, "Play Limit"        },
    { "daily_limit",      FIELD_APP_LIMITS, "Daily Limit"       },
    { "download_limit",   FIELD_APP_LIMITS, "Download Limit"    },
    { "stream_limit",     FIELD_APP_LIMITS, "Stream Limit"      },
    { "device_limit",     FIELD_APP_LIMITS, "Device Limit"      },
    { "upload_limit",     FIELD_APP_LIMITS, "Upload Limit"      },
    { "storage_limit",    FIELD_APP_LIMITS, "Storage Limit"     },
    { "quota",            FIELD_APP_LIMITS, "Quota"             },
    { "remaining",        FIELD_APP_LIMITS, "Remaining"         },
    { "usage_left",       FIELD_APP_LIMITS, "Usage Left"        },
    { "rate_limit",       FIELD_APP_LIMITS, "Rate Limit"        },
    { "max_devices",      FIELD_APP_LIMITS, "Max Devices"       },
    { "concurrent",       FIELD_APP_LIMITS, "Concurrent"        },
    { "simultaneous",     FIELD_APP_LIMITS, "Simultaneous"      },
    { "message_limit",    FIELD_APP_LIMITS, "Message Limit"     },           /* AI apps */
    { "token_limit",      FIELD_APP_LIMITS, "Token Limit"       },
    { "request_limit",    FIELD_APP_LIMITS, "Request Limit"     },
    { "generation_limit", FIELD_APP_LIMITS, "Generation Limit"  },

    /* ══════════════════════════════════════════════════════════════════
     * APP — TRIAL / EXPIRY
     * ══════════════════════════════════════════════════════════════════ */
    { "is_trial",         FIELD_APP_TRIAL, "Is Trial"           },
    { "trial",            FIELD_APP_TRIAL, "Trial"              },
    { "trial_days",       FIELD_APP_TRIAL, "Trial Days"         },
    { "days_remaining",   FIELD_APP_TRIAL, "Days Remaining"     },
    { "days_left",        FIELD_APP_TRIAL, "Days Left"          },
    { "trial_end",        FIELD_APP_TRIAL, "Trial End"          },
    { "expires_at",       FIELD_APP_TRIAL, "Expires At"         },
    { "expiry",           FIELD_APP_TRIAL, "Expiry"             },
    { "expired",          FIELD_APP_TRIAL, "Expired"            },
    { "is_expired",       FIELD_APP_TRIAL, "Is Expired"         },
    { "grace_period",     FIELD_APP_TRIAL, "Grace Period"       },
    { "in_grace",         FIELD_APP_TRIAL, "In Grace Period"    },
    { "free_tier",        FIELD_APP_TRIAL, "Free Tier"          },
    { "cancel_at",        FIELD_APP_TRIAL, "Cancel At"          },
    { "renew_at",         FIELD_APP_TRIAL, "Renew At"           },
    { "next_billing",     FIELD_APP_TRIAL, "Next Billing"       },

    /* ══════════════════════════════════════════════════════════════════
     * APP — ADS
     * ══════════════════════════════════════════════════════════════════ */
    { "show_ads",         FIELD_APP_ADS, "Show Ads"             },
    { "has_ads",          FIELD_APP_ADS, "Has Ads"              },
    { "ad_free",          FIELD_APP_ADS, "Ad Free"              },
    { "no_ads",           FIELD_APP_ADS, "No Ads"               },
    { "ads_enabled",      FIELD_APP_ADS, "Ads Enabled"          },
    { "ad_supported",     FIELD_APP_ADS, "Ad Supported"         },
    { "ad_interval",      FIELD_APP_ADS, "Ad Interval"          },
    { "ad_frequency",     FIELD_APP_ADS, "Ad Frequency"         },
    { "rewarded_ad",      FIELD_APP_ADS, "Rewarded Ads"         },
    { "interstitial",     FIELD_APP_ADS, "Interstitial Ads"     },
    { "banner_ad",        FIELD_APP_ADS, "Banner Ads"           },

    /* ══════════════════════════════════════════════════════════════════
     * APP — QUALITY / BITRATE
     * ══════════════════════════════════════════════════════════════════ */
    { "bitrate",          FIELD_APP_QUALITY, "Bitrate"           },
    { "audio_quality",    FIELD_APP_QUALITY, "Audio Quality"     },
    { "video_quality",    FIELD_APP_QUALITY, "Video Quality"     },
    { "resolution",       FIELD_APP_QUALITY, "Resolution"        },
    { "quality_level",    FIELD_APP_QUALITY, "Quality Level"     },
    { "max_bitrate",      FIELD_APP_QUALITY, "Max Bitrate"       },
    { "stream_quality",   FIELD_APP_QUALITY, "Stream Quality"    },
    { "download_quality", FIELD_APP_QUALITY, "Download Quality"  },
    { "sample_rate",      FIELD_APP_QUALITY, "Sample Rate"       },
    { "codec",            FIELD_APP_QUALITY, "Codec"             },
    { "format",           FIELD_APP_QUALITY, "Format"            },

    /* ══════════════════════════════════════════════════════════════════
     * APP — SOCIAL
     * likes, followers, views — common in social and creator apps
     * ══════════════════════════════════════════════════════════════════ */
    { "follower",         FIELD_APP_SOCIAL, "Followers"          },
    { "following",        FIELD_APP_SOCIAL, "Following"          },
    { "like",             FIELD_APP_SOCIAL, "Likes"              },
    { "heart",            FIELD_APP_SOCIAL, "Hearts"             },
    { "view_count",       FIELD_APP_SOCIAL, "Views"              },
    { "play_count",       FIELD_APP_SOCIAL, "Plays"              },
    { "stream_count",     FIELD_APP_SOCIAL, "Streams"            },
    { "monthly_listener", FIELD_APP_SOCIAL, "Monthly Listeners"  },
    { "subscriber",       FIELD_APP_SOCIAL, "Subscribers"        },
    { "match",            FIELD_APP_SOCIAL, "Matches"            },           /* Tinder */
    { "super_like",       FIELD_APP_SOCIAL, "Super Likes"        },           /* Tinder */
    { "boost",            FIELD_APP_SOCIAL, "Boost"              },           /* Tinder */
    { "rose",             FIELD_APP_SOCIAL, "Roses"              },           /* Hinge */
    { "streak",           FIELD_APP_SOCIAL, "Streak"             },           /* Snapchat/Duolingo */
    { "karma",            FIELD_APP_SOCIAL, "Karma"              },
    { "clap",             FIELD_APP_SOCIAL, "Claps"              },

    /* ══════════════════════════════════════════════════════════════════
     * APP — COMMERCE / WALLET
     * ══════════════════════════════════════════════════════════════════ */
    { "wallet",           FIELD_APP_COMMERCE, "Wallet"            },
    { "balance",          FIELD_APP_COMMERCE, "Balance"           },
    { "credit",           FIELD_APP_COMMERCE, "Credits"           },
    { "point",            FIELD_APP_COMMERCE, "Points"            },
    { "reward",           FIELD_APP_COMMERCE, "Rewards"           },
    { "cashback",         FIELD_APP_COMMERCE, "Cashback"          },
    { "voucher",          FIELD_APP_COMMERCE, "Vouchers"          },
    { "coupon",           FIELD_APP_COMMERCE, "Coupons"           },
    { "promo",            FIELD_APP_COMMERCE, "Promo"             },
    { "discount",         FIELD_APP_COMMERCE, "Discount"          },
    { "token",            FIELD_APP_COMMERCE, "Tokens"            },
    { "stamp",            FIELD_APP_COMMERCE, "Stamps"            },
    { "miles",            FIELD_APP_COMMERCE, "Miles"             },
    { "coin",             FIELD_APP_COMMERCE, "Coins"             },
    { "bean",             FIELD_APP_COMMERCE, "Beans"             },           /* TikTok Live */
    { "gift",             FIELD_APP_COMMERCE, "Gifts"             },
    { "donation",         FIELD_APP_COMMERCE, "Donations"         },
    { "tip",              FIELD_APP_COMMERCE, "Tips"              },
    { "purchase_count",   FIELD_APP_COMMERCE, "Purchases"         },
    { "spend",            FIELD_APP_COMMERCE, "Spend"             },
};

const size_t PHANTOM_FIELD_DICT_LEN =
    sizeof(PHANTOM_FIELD_DICT) / sizeof(PHANTOM_FIELD_DICT[0]);
