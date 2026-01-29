/**
 * @file surface.h
 * @brief MC11: The Surface - Zero-Oversight Physics-Based UI Compositor
 *
 * The Surface is SERAPH's UI paradigm. There is no desktop, no windows.
 * There is only:
 *   - THE LOCUS: The center of your attention
 *   - THE ORBS: Applications as floating spheres
 *   - THE PHYSICS: Everything reacts, breathes, flows
 *
 * "A user does not care about 'Hyper-Duals' or 'Spectral Arenas.'
 *  A user cares about INTENT."
 *
 * VISUAL THEME: Dark Seraphic
 * ===========================
 * The Surface uses a dark, ethereal theme inspired by six-winged seraphim:
 *   - Deep navy/black background (#0D0E14)
 *   - Steel blue-gray orbs (#6B7B8E - #8A9AAD)
 *   - Silver highlights (#B8C4D0 - #D4DCE6)
 *   - Subtle purple undertones (#2A2D36)
 *   - Pale blue-white glow (#C8D4E0)
 *
 * The aesthetic is: Dark, ethereal, contemplative, precise.
 */

#ifndef SERAPH_SURFACE_H
#define SERAPH_SURFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "seraph/void.h"
#include "seraph/vbit.h"
#include "seraph/galactic.h"
#include "seraph/glyph.h"
#include "seraph/sovereign.h"
#include "seraph/chronon.h"
#include "seraph/capability.h"
#include "seraph/atlas.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Theme Constants (Dark Seraphic Palette)
 *============================================================================*/

/**
 * @brief RGBA color representation (32-bit)
 */
typedef struct {
    uint8_t r, g, b, a;
} Seraph_Color;

/** Create color from RGB */
#define SERAPH_RGB(r, g, b) ((Seraph_Color){ (r), (g), (b), 255 })

/** Create color from RGBA */
#define SERAPH_RGBA(r, g, b, a) ((Seraph_Color){ (r), (g), (b), (a) })

/** Dark Seraphic Theme Colors */
#define SERAPH_THEME_BACKGROUND      SERAPH_RGB(0x0D, 0x0E, 0x14)  /**< Deep navy-black */
#define SERAPH_THEME_BACKGROUND_ALT  SERAPH_RGB(0x12, 0x14, 0x1A)  /**< Slightly lighter */
#define SERAPH_THEME_ORB_BASE        SERAPH_RGB(0x6B, 0x7B, 0x8E)  /**< Steel blue-gray */
#define SERAPH_THEME_ORB_HOVER       SERAPH_RGB(0x8A, 0x9A, 0xAD)  /**< Lighter steel */
#define SERAPH_THEME_ORB_ACTIVE      SERAPH_RGB(0xB8, 0xC4, 0xD0)  /**< Silver accent */
#define SERAPH_THEME_HIGHLIGHT       SERAPH_RGB(0xD4, 0xDC, 0xE6)  /**< Pale silver */
#define SERAPH_THEME_GLOW            SERAPH_RGB(0xC8, 0xD4, 0xE0)  /**< Blue-white glow */
#define SERAPH_THEME_SHADOW          SERAPH_RGB(0x2A, 0x2D, 0x36)  /**< Dark purple-gray */
#define SERAPH_THEME_LOCUS           SERAPH_RGB(0x4A, 0x5A, 0x70)  /**< Muted center */

/** Convert Seraph_Color to packed uint32_t (RGBA) */
static inline uint32_t seraph_color_to_u32(Seraph_Color c) {
    return ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) |
           ((uint32_t)c.b << 8) | (uint32_t)c.a;
}

/** Interpolate between two colors */
static inline Seraph_Color seraph_color_lerp(Seraph_Color a, Seraph_Color b, float t) {
    return (Seraph_Color){
        .r = (uint8_t)(a.r + (b.r - a.r) * t),
        .g = (uint8_t)(a.g + (b.g - a.g) * t),
        .b = (uint8_t)(a.b + (b.b - a.b) * t),
        .a = (uint8_t)(a.a + (b.a - a.a) * t)
    };
}

/*============================================================================
 * Surface Configuration
 *============================================================================*/

/**
 * @brief Surface configuration options
 */
typedef struct {
    bool instant_mode;              /**< Skip physics for gaming/low-latency */
    bool physics_enabled;           /**< Enable orb physics simulation */
    float magnetism_strength;       /**< How strongly orbs attract to cursor */
    float swell_factor;             /**< How much orbs grow on approach */
    float damping_factor;           /**< Velocity damping (friction): 0.0=none, 0.9=heavy */
    float merge_threshold;          /**< Distance for liquid merging */
    uint64_t preview_duration_ms;   /**< Intent preview phase duration */
    uint64_t commit_delay_ms;       /**< Time before action commits */
    uint64_t undo_bubble_duration;  /**< Time undo option is available */
} Seraph_Surface_Config;

/** Default configuration */
#define SERAPH_SURFACE_CONFIG_DEFAULT ((Seraph_Surface_Config){ \
    .instant_mode = false,                                      \
    .physics_enabled = true,                                    \
    .magnetism_strength = 1.0f,                                 \
    .swell_factor = 5.0f,                                       \
    .damping_factor = 0.95f,                                    \
    .merge_threshold = 2.0f,                                    \
    .preview_duration_ms = 300,                                 \
    .commit_delay_ms = 500,                                     \
    .undo_bubble_duration = 2000                                \
})

/*============================================================================
 * The Locus (Center of Focus)
 *============================================================================*/

/**
 * @brief The Locus - the center of user attention
 *
 * A gravity well representing your FOCUS. It does not display information.
 * It ATTRACTS information. Things you care about are pulled toward the Locus.
 * Things you don't care about drift away into void.
 */
typedef struct {
    Seraph_Galactic position_x;     /**< X axis: (x, dx/dt) - position and velocity */
    Seraph_Galactic position_y;     /**< Y axis: (y, dy/dt) - position and velocity */
    Seraph_Galactic gravity;        /**< Attraction strength field */
    Seraph_Chronon last_update;     /**< When position was last changed */
    Seraph_Vbit active;             /**< Is the locus being interacted with? */
} Seraph_Locus;

/*============================================================================
 * The Orb (Application Manifestation)
 *============================================================================*/

/**
 * @brief Orb state enumeration
 */
typedef enum {
    SERAPH_ORB_IDLE = 0,            /**< Normal state, in orbit */
    SERAPH_ORB_HOVER = 1,           /**< Cursor is near */
    SERAPH_ORB_SWELLING = 2,        /**< Growing from approach */
    SERAPH_ORB_EXPANDING = 3,       /**< Expanding to fullscreen */
    SERAPH_ORB_FULLSCREEN = 4,      /**< Fullscreen (active app) */
    SERAPH_ORB_CONTRACTING = 5,     /**< Shrinking back to orb */
    SERAPH_ORB_PERIPHERAL = 6,      /**< Minimized to edge */
    SERAPH_ORB_VOID = 0xFF          /**< Invalid/dead orb */
} Seraph_Orb_State;

/**
 * @brief The Orb - an application's visual manifestation
 *
 * Each Orb represents an APPLICATION - but not in the traditional sense.
 * An Orb is a CAPABILITY REALM (Sovereign) with a visual manifestation.
 *
 * Properties:
 *   - SIZE reflects importance/recency (larger = more relevant)
 *   - BRIGHTNESS reflects activity (brighter = doing something)
 *   - ORBIT DISTANCE reflects relationship to current task
 *   - GLOW reflects notifications/state changes
 */
typedef struct {
    /* Identity */
    Seraph_Capability sovereign_cap;    /**< Capability to underlying Sovereign */
    uint64_t orb_id;                    /**< Unique orb identifier */

    /* Visual properties (Hyper-Dual for automatic physics) */
    Seraph_Galactic position_x;         /**< X: (x, dx/dt) position and velocity */
    Seraph_Galactic position_y;         /**< Y: (y, dy/dt) position and velocity */
    Seraph_Galactic radius;             /**< (r, dr/dt) radius and rate of change */
    Seraph_Galactic brightness;         /**< (b, db/dt) brightness and rate */
    Seraph_Galactic glow;               /**< (g, dg/dt) glow intensity and rate */

    /* Orbital mechanics */
    Seraph_Q128 orbit_distance;         /**< Distance from Locus when at rest */
    Seraph_Q128 orbit_angle;            /**< Angular position in orbit */
    Seraph_Q128 orbit_velocity;         /**< Angular velocity */

    /* State */
    Seraph_Orb_State state;             /**< Current orb state */
    Seraph_Vbit visible;                /**< Is this orb visible? */
    Seraph_Vbit focused;                /**< Is cursor near this orb? */
    uint32_t notifications;             /**< Pending notification count */

    /* Colors (from theme, can be customized) */
    Seraph_Color color_base;            /**< Base orb color */
    Seraph_Color color_glow;            /**< Glow color */

    /* Base visual parameters (before physics) */
    float base_radius;                  /**< Radius at rest */

} Seraph_Orb;

/** Maximum orbs on the Surface */
#define SERAPH_SURFACE_MAX_ORBS 64

/** VOID orb constant */
#define SERAPH_ORB_VOID_CONST ((Seraph_Orb){ \
    .orb_id = SERAPH_VOID_U64,               \
    .state = SERAPH_ORB_VOID                 \
})

/*============================================================================
 * Persistence Structures (Atlas Integration)
 *============================================================================
 *
 * "A UI that survives the apocalypse."
 *
 * When an Orb moves, its position is written to Atlas via the Genesis
 * transaction log. When the system restarts (even after a crash), the
 * Surface reconstructs itself from Atlas. Every position change persists.
 *
 * Design decisions:
 *   - ONLY primal values persist (positions, not velocities)
 *   - Velocities are transient; physics restarts from stationary state
 *   - Galactic tangent components are recomputed from physics
 *   - This gives semantic "UI starts from where it was, but at rest"
 */

/** Magic number for Surface persistent state: "SRFCSURF" */
#define SERAPH_SURFACE_MAGIC  0x5352464353555246ULL

/** Surface persistence format version */
#define SERAPH_SURFACE_VERSION  1

/**
 * @brief Persistent Orb state - what survives a reboot
 *
 * Only the essential state is persisted:
 *   - Identity (orb_id)
 *   - Position (Q128, not Galactic - no velocity)
 *   - Orbital parameters
 *   - Visual customization
 *   - State flags
 *
 * Transient properties (velocity, glow animation) are NOT persisted.
 * They are recomputed by physics on restart.
 */
typedef struct {
    /** Unique orb identifier */
    uint64_t orb_id;

    /** Position as Q128 (primal only, no velocity) */
    Seraph_Q128 position_x;
    Seraph_Q128 position_y;

    /** Orbital mechanics */
    Seraph_Q128 orbit_distance;
    Seraph_Q128 orbit_angle;
    Seraph_Q128 orbit_velocity;

    /** Visual properties */
    float base_radius;
    Seraph_Orb_State state;

    /** Colors (customized from theme) */
    Seraph_Color color_base;
    Seraph_Color color_glow;

    /** Sovereign capability base (for re-association) */
    uint64_t sovereign_cap_base;

    /** Reserved for future use */
    uint8_t _reserved[16];
} Seraph_Surface_Persistent_Orb;

/**
 * @brief Persistent Surface state - the root structure in Atlas
 *
 * This structure is allocated in Atlas and pointed to from Genesis.
 * It contains everything needed to reconstruct the Surface after a restart.
 */
typedef struct {
    /** Magic number for validation (SERAPH_SURFACE_MAGIC) */
    uint64_t magic;

    /** Format version */
    uint64_t version;

    /** Display dimensions */
    uint32_t width;
    uint32_t height;

    /** Locus position (center of attention) */
    Seraph_Q128 locus_x;
    Seraph_Q128 locus_y;

    /** Configuration settings */
    Seraph_Surface_Config config;

    /** Number of orbs */
    uint32_t orb_count;

    /** Reserved for alignment and future use */
    uint32_t _reserved1;
    uint64_t _reserved2;

    /** Last modification chronon */
    Seraph_Chronon last_modified;

    /** Persistent orb array */
    Seraph_Surface_Persistent_Orb orbs[SERAPH_SURFACE_MAX_ORBS];
} Seraph_Surface_Persistent_State;

/*============================================================================
 * Intent Detection (Three-Phase UI Model)
 *============================================================================*/

/**
 * @brief Intent phase enumeration
 *
 * The three-phase model prevents accidental actions:
 *   1. PREVIEW: User is approaching, show potential action
 *   2. COMMIT: User has committed to the action
 *   3. UNDO: Brief window to reverse the action
 */
typedef enum {
    SERAPH_INTENT_NONE = 0,         /**< No intent detected */
    SERAPH_INTENT_PREVIEW = 1,      /**< Showing potential action */
    SERAPH_INTENT_COMMIT = 2,       /**< Action committed */
    SERAPH_INTENT_UNDO = 3,         /**< Undo window active */
    SERAPH_INTENT_VOID = 0xFF       /**< Invalid state */
} Seraph_Intent_Phase;

/**
 * @brief Intent state tracking
 */
typedef struct {
    Seraph_Intent_Phase phase;
    int32_t source_orb;             /**< Initiating orb index (-1 if none) */
    int32_t target_orb;             /**< Target orb index (-1 if none) */
    Seraph_Chronon phase_start;     /**< When current phase began */
    float proximity;                /**< How close (0.0 = far, 1.0 = touching) */
} Seraph_Intent_State;

/*============================================================================
 * The Surface (Complete UI State)
 *============================================================================*/

/**
 * @brief The Surface - SERAPH's complete UI compositor
 *
 * The Surface contains:
 *   - THE LOCUS: Center of attention
 *   - THE ORBS: All application manifestations
 *   - THE PHYSICS: Attraction, swelling, merging
 *   - THE INPUT: Cursor/gaze position
 */
typedef struct {
    /* The center of attention */
    Seraph_Locus locus;

    /* All application orbs */
    Seraph_Orb orbs[SERAPH_SURFACE_MAX_ORBS];
    uint32_t orb_count;

    /* Input state (cursor/gaze position with velocity for anticipation) */
    Seraph_Galactic cursor_x;           /**< X: (x, dx/dt) cursor position and velocity */
    Seraph_Galactic cursor_y;           /**< Y: (y, dy/dt) cursor position and velocity */
    Seraph_Vbit cursor_present;         /**< Is there active input? */

    /* Currently expanded orb (-1 if none) */
    int32_t expanded_orb_index;

    /* Intent detection state */
    Seraph_Intent_State intent;

    /* Configuration */
    Seraph_Surface_Config config;

    /* Temporal state */
    Seraph_Chronon current_chronon;
    Seraph_Chronon last_physics_update;

    /* Display dimensions */
    uint32_t width;
    uint32_t height;

    /* Atlas persistence (optional - NULL if not connected) */
    Seraph_Atlas* atlas;                        /**< Connected Atlas instance */
    Seraph_Surface_Persistent_State* persistent; /**< Persistent state in Atlas */

    /* Subsystem initialized flag */
    bool initialized;

} Seraph_Surface;

/*============================================================================
 * Surface Operations
 *============================================================================*/

/**
 * @brief Initialize the Surface
 * @param surface Surface to initialize
 * @param width Display width
 * @param height Display height
 * @return TRUE on success, VOID on failure
 */
Seraph_Vbit seraph_surface_init(
    Seraph_Surface* surface,
    uint32_t width,
    uint32_t height
);

/**
 * @brief Initialize with custom configuration
 */
Seraph_Vbit seraph_surface_init_with_config(
    Seraph_Surface* surface,
    uint32_t width,
    uint32_t height,
    Seraph_Surface_Config config
);

/**
 * @brief Destroy the Surface and free resources
 */
void seraph_surface_destroy(Seraph_Surface* surface);

/*============================================================================
 * Orb Operations
 *============================================================================*/

/**
 * @brief Create an orb for a Sovereign application
 * @param surface The surface
 * @param sovereign_cap Capability to the Sovereign
 * @param orbit_distance Initial distance from locus
 * @param orbit_angle Initial angle in orbit (radians)
 * @return Orb index, or -1 on failure
 */
int32_t seraph_surface_create_orb(
    Seraph_Surface* surface,
    Seraph_Capability sovereign_cap,
    float orbit_distance,
    float orbit_angle
);

/**
 * @brief Remove an orb from the surface
 */
Seraph_Vbit seraph_surface_remove_orb(
    Seraph_Surface* surface,
    int32_t orb_index
);

/**
 * @brief Get orb by index
 */
Seraph_Orb* seraph_surface_get_orb(
    Seraph_Surface* surface,
    int32_t orb_index
);

/**
 * @brief Find orb by Sovereign capability
 * @return Orb index, or -1 if not found
 */
int32_t seraph_surface_find_orb(
    Seraph_Surface* surface,
    Seraph_Capability sovereign_cap
);

/*============================================================================
 * Input Operations
 *============================================================================*/

/**
 * @brief Update cursor position
 */
void seraph_surface_update_cursor(
    Seraph_Surface* surface,
    float x,
    float y
);

/**
 * @brief Update cursor with velocity
 */
void seraph_surface_update_cursor_with_velocity(
    Seraph_Surface* surface,
    float x,
    float y,
    float vel_x,
    float vel_y
);

/**
 * @brief Set cursor presence (visible/hidden)
 */
void seraph_surface_set_cursor_present(
    Seraph_Surface* surface,
    Seraph_Vbit present
);

/*============================================================================
 * Physics Operations
 *============================================================================*/

/**
 * @brief Run one physics step
 *
 * Updates all orb positions, sizes, and detects intent.
 */
void seraph_surface_physics_step(
    Seraph_Surface* surface,
    Seraph_Chronon delta_chronon
);

/**
 * @brief Compute attraction force between cursor and orb
 */
Seraph_Galactic seraph_surface_attraction(
    Seraph_Galactic cursor_pos,
    Seraph_Galactic orb_pos,
    float strength
);

/**
 * @brief Compute swelling radius based on cursor proximity
 */
float seraph_surface_swell_radius(
    float cursor_x,
    float cursor_y,
    float orb_x,
    float orb_y,
    float base_radius,
    float swell_factor
);

/**
 * @brief Compute distance between cursor and orb
 */
float seraph_surface_orb_distance(
    Seraph_Surface* surface,
    int32_t orb_index
);

/*============================================================================
 * Intent Detection
 *============================================================================*/

/**
 * @brief Detect which orb (if any) the user intends to activate
 * @return Orb index, or -1 if no clear intent
 */
int32_t seraph_surface_detect_intent(Seraph_Surface* surface);

/**
 * @brief Get current intent state
 */
Seraph_Intent_State seraph_surface_get_intent(Seraph_Surface* surface);

/**
 * @brief Cancel current intent (e.g., from shake gesture)
 */
void seraph_surface_cancel_intent(Seraph_Surface* surface);

/*============================================================================
 * Expansion/Contraction (App Launch/Exit)
 *============================================================================*/

/**
 * @brief Expand an orb to fullscreen (launch application)
 */
Seraph_Vbit seraph_surface_expand_orb(
    Seraph_Surface* surface,
    int32_t orb_index
);

/**
 * @brief Contract the currently expanded orb back to orb form
 */
Seraph_Vbit seraph_surface_contract_current(Seraph_Surface* surface);

/**
 * @brief Check if an orb is expanded
 */
bool seraph_surface_is_orb_expanded(
    Seraph_Surface* surface,
    int32_t orb_index
);

/*============================================================================
 * Rendering
 *============================================================================*/

/**
 * @brief Render the Surface to a framebuffer
 *
 * Uses the Glyph SDF system for smooth, resolution-independent rendering.
 */
void seraph_surface_render(
    Seraph_Surface* surface,
    uint32_t* framebuffer,
    uint32_t width,
    uint32_t height
);

/**
 * @brief Render a single orb
 */
void seraph_surface_render_orb(
    Seraph_Orb* orb,
    uint32_t* framebuffer,
    uint32_t width,
    uint32_t height,
    float center_x,
    float center_y
);

/**
 * @brief Render the locus
 */
void seraph_surface_render_locus(
    Seraph_Locus* locus,
    uint32_t* framebuffer,
    uint32_t width,
    uint32_t height
);

/*============================================================================
 * Atlas Persistence Operations
 *============================================================================
 *
 * These functions connect the Surface to Atlas for persistent UI state.
 * When connected, Orb position changes are automatically written to the
 * Genesis transaction log and survive crashes, reboots, and apocalypse.
 */

/**
 * @brief Connect Surface to an Atlas instance for persistence
 *
 * After connection, orb movements can be persisted via seraph_surface_persist_orb().
 * If Atlas already contains Surface state, it is NOT loaded automatically.
 * Use seraph_surface_init_from_atlas() to load existing state.
 *
 * @param surface The surface to connect
 * @param atlas The Atlas instance to use for persistence
 * @return TRUE on success, VOID on failure
 */
Seraph_Vbit seraph_surface_set_atlas(
    Seraph_Surface* surface,
    Seraph_Atlas* atlas
);

/**
 * @brief Initialize Surface from existing Atlas state
 *
 * Loads Surface state from Atlas, reconstructing all Orbs at their
 * persisted positions. This is the "restore from apocalypse" function.
 *
 * @param surface Surface to initialize (should be uninitialized or destroyed)
 * @param atlas Atlas containing persistent Surface state
 * @return TRUE on success, FALSE if no state exists, VOID on error
 */
Seraph_Vbit seraph_surface_init_from_atlas(
    Seraph_Surface* surface,
    Seraph_Atlas* atlas
);

/**
 * @brief Persist a single orb's state to Atlas
 *
 * Writes the orb's current position to Atlas via a transaction.
 * Call this when an orb finishes moving (velocity drops to zero)
 * or when explicit persistence is desired.
 *
 * This is the core "UI survives apocalypse" mechanism.
 *
 * @param surface The surface
 * @param orb_index Index of the orb to persist
 * @return TRUE on success, VOID on failure
 */
Seraph_Vbit seraph_surface_persist_orb(
    Seraph_Surface* surface,
    int32_t orb_index
);

/**
 * @brief Persist entire Surface state to Atlas
 *
 * Writes ALL orb positions and Surface configuration to Atlas.
 * More expensive than single-orb persistence but useful for
 * initial save or batch operations.
 *
 * @param surface The surface to persist
 * @return TRUE on success, VOID on failure
 */
Seraph_Vbit seraph_surface_persist(Seraph_Surface* surface);

/**
 * @brief Check if Surface has persistent state in Atlas
 *
 * @param atlas The Atlas to check
 * @return TRUE if persistent state exists, FALSE if not
 */
bool seraph_surface_has_persistent_state(Seraph_Atlas* atlas);

/**
 * @brief Get persistent state from Atlas (read-only access)
 *
 * @param atlas The Atlas to read from
 * @return Pointer to persistent state, or NULL if none exists
 */
Seraph_Surface_Persistent_State* seraph_surface_get_persistent_state(
    Seraph_Atlas* atlas
);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Check if surface is valid (initialized)
 */
static inline bool seraph_surface_is_valid(const Seraph_Surface* surface) {
    return surface != NULL && surface->initialized;
}

/**
 * @brief Check if orb state indicates visibility
 */
static inline bool seraph_orb_state_is_visible(Seraph_Orb_State state) {
    return state != SERAPH_ORB_VOID && state != SERAPH_ORB_PERIPHERAL;
}

/**
 * @brief Check if orb state indicates interactivity
 */
static inline bool seraph_orb_state_is_interactive(Seraph_Orb_State state) {
    return state == SERAPH_ORB_IDLE ||
           state == SERAPH_ORB_HOVER ||
           state == SERAPH_ORB_SWELLING;
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SURFACE_H */
