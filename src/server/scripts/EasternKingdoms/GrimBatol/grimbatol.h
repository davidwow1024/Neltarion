#ifndef DEF_GRIM_BATOL_H
#define DEF_GRIM_BATOL_H

enum Data
{
    DATA_GENERAL_UMBRISS_EVENT,
    DATA_FORGEMASTER_THRONGUS_EVENT,
    DATA_DRAHGA_SHADOWBURNER_EVENT,
    DATA_ERUDAX_EVENT,
    DATA_TEAM_IN_INSTANCE,
};

enum Data64
{
    DATA_GENERAL_UMBRISS,
    DATA_FORGEMASTER_THRONGUS,
    DATA_DRAHGA_SHADOWBURNER,
    DATA_ERUDAX,
};

enum CreatureIds
{
    // Dungeon Bosses
    BOSS_GENERAL_UMBRISS                = 39625,
    BOSS_FORGEMASTER_THRONGUS           = 40177,
    BOSS_DRAHGA_SHADOWBURNER            = 40319,
    BOSS_ERUDAX                         = 40484,

    // Trash mobs
    NPC_ASCENDED_FLAMESEEKER            = 39415,
    NPC_ASCENDED_ROCKBREAKER            = 40272,
    NPC_ASCENDED_WATERLASHER            = 40273,
    NPC_ASCENDED_WINDWALKER             = 39414,
    NPC_AZURBORNE_GUARDIAN              = 39854,
    NPC_AZURBORNE_SEER                  = 39855,
    NPC_AZURBORNE_WARLORD               = 39909,
    NPC_BATTERED_RED_DRAKE              = 48523,
    NPC_CRIMSONBORNE_GUARDIAN           = 39381,
    NPC_CRIMSONBORNE_SEER               = 40290,
    NPC_CRIMSONBORNE_WARLORD            = 39626,
    NPC_ENSLAVED_BURNING_EMBER          = 39892,
    NPC_ENSLAVED_ROCK_ELEMENTAL         = 39900,
    NPC_ENSLAVED_THUNDER_SPIRIT         = 40269,
    NPC_ENSLAVED_GRONN_BRUTE            = 40166,
    NPC_ENSLAVED_WATER_SPIRIT           = 39961,
    NPC_TWILIGHT_FIRE_CATCHER           = 39870,
    NPC_TWILIGHT_SHADOW_WEAVER          = 39954,
    NPC_TWILIGHT_STORM_BREAKER          = 39962,
    NPC_TWILIGHT_THUNDER_CALLER         = 40270,
    NPC_TWILIGHT_WYRM_CALLER            = 39873,

    // Various NPCs
    NPC_VELASTRASZA                     = 50390,
    NPC_VALIONA                         = 40320,
    NPC_BALEFLAME                       = 50387,
    NPC_FARSEER_TOORANU                 = 50385,

    // General Umbriss
    NPC_GROUND_SIEGE_STALKER            = 40030,
    NPC_MALIGNANT_TROGG                 = 39984,
    NPC_TROGG_DWELLER                   = 45467,
    NPC_TRIGGER_BLITZ                   = 40040,

    // Forgemaster Throngus
    NPC_FIRE_PATCH                      = 48711,
    NPC_TWILIGHT_ARCHER                 = 40197,
    NPC_CAVE_IN_STALKER                 = 40228,
    NPC_FIXATE_STALKER                  = 40255,

    // Drahga Shadowburner
    NPC_INVOCATION_OF_THE_FLAME_STALKER = 40355,
    NPC_INVOKED_FLAMING_SPIRIT          = 40357,
    NPC_SEEPING_TWILIGHT_TRIGGER        = 40365,
    NPC_DEVOURING_FLAMES                = 48798,

    // Erudax
    NPC_FACELESS_LEFT                   = 48844,
    NPC_FACELESS_RIGHT                  = 40600,
    NPC_FACELESS_PORTAL_STALKER         = 44314,
    NPC_ALEXSTRASZAS_EGG                = 40486,
    NPC_TWILIGHT_HATCHLING              = 39388,
    NPC_TWILIGHT_EGG                    = 40846,
    NPC_SHADOW_GALE_STALKER             = 40567
};

enum GameObjectIds
{
    GO_DWARVEN_BONE_PILE                = 207411,
};

enum Achievements
{
    ACHIEV_UMBRAGE_FOR_UMBRISS          = 5297,
};

#endif
