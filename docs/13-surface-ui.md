# MC11: Surface - The Seraphic UI Compositor

## Overview

The Surface is SERAPH's revolutionary user interface system. Unlike traditional window managers that display static rectangles, the Surface is a **physics-based compositor** where applications manifest as **Orbs** - luminous spheres that float in a gravitational field centered on the **Locus** (your point of attention).

## Plain English Explanation

Imagine a 3D space where your focus is a gravity well at the center. Applications aren't windows - they're glowing orbs that orbit around your attention. When you look at (or move toward) an orb, it senses your approach and swells up in anticipation. Click it, and it expands to fill your view. When you're done, it contracts back into its orbital position.

The magic is in the **anticipation**: because every position and cursor tracks velocity using Galactic numbers (automatic differentiation), the UI can predict where you're *going*, not just where you *are*. Orbs start swelling before you arrive if you're moving quickly toward them.

## Architecture

### The Locus

The Locus is the **center of attention** - a gravity well that pulls relevant information toward you.

```c
typedef struct {
    Seraph_Galactic position_x;     /**< X axis: (x, dx/dt) */
    Seraph_Galactic position_y;     /**< Y axis: (y, dy/dt) */
    Seraph_Galactic gravity;        /**< Attraction strength field */
    Seraph_Chronon last_update;     /**< When position was last changed */
    Seraph_Vbit active;             /**< Is the locus being interacted with? */
} Seraph_Locus;
```

The Locus doesn't display information - it **attracts** information. Things you care about are pulled toward the Locus. Things you don't care about drift away into the void.

### The Orb

Each application manifests as an Orb - a luminous sphere with physical properties.

```c
typedef struct {
    /* Identity */
    Seraph_Capability sovereign_cap;    /**< Capability to underlying Sovereign */
    uint64_t orb_id;                    /**< Unique orb identifier */

    /* Visual properties (Hyper-Dual for automatic physics) */
    Seraph_Galactic position_x;         /**< X: (x, dx/dt) position and velocity */
    Seraph_Galactic position_y;         /**< Y: (y, dy/dt) position and velocity */
    Seraph_Galactic radius;             /**< (r, dr/dt) radius and rate */
    Seraph_Galactic brightness;         /**< (b, db/dt) brightness and rate */
    Seraph_Galactic glow;               /**< (g, dg/dt) glow intensity and rate */

    /* Orbital mechanics */
    Seraph_Q128 orbit_distance;         /**< Distance from Locus when at rest */
    Seraph_Q128 orbit_angle;            /**< Angular position in orbit */
    Seraph_Q128 orbit_velocity;         /**< Angular velocity */

    /* State */
    Seraph_Orb_State state;             /**< Current orb state */
    /* ... */
} Seraph_Orb;
```

#### Orb States

```c
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
```

## The Galactic Physics Revolution

### Why Separate Galactics for Each Axis

Traditional UI systems store position as (x, y) pairs. SERAPH stores position as **two Galactic numbers**:

- `position_x`: Contains both X position (primal) AND X velocity (tangent)
- `position_y`: Contains both Y position (primal) AND Y velocity (tangent)

This enables **automatic differentiation** for each axis independently:

```c
/* Extract current position */
float x = seraph_q128_to_double(orb->position_x.primal);
float y = seraph_q128_to_double(orb->position_y.primal);

/* Apply acceleration to each axis independently */
orb->position_x = seraph_galactic_integrate_accel(orb->position_x, accel_x, dt);
orb->position_y = seraph_galactic_integrate_accel(orb->position_y, accel_y, dt);
```

### Anticipation Physics

The cursor also uses 2D Galactics, tracking both position and velocity:

```c
/* Cursor has separate X and Y Galactics */
surface->cursor_x = seraph_galactic_from_pos_vel(x, vel_x);
surface->cursor_y = seraph_galactic_from_pos_vel(y, vel_y);
```

This enables **predictive UI**:

```c
/* Predict cursor position 100ms into the future */
Seraph_Q128 lookahead = seraph_q128_from_double(0.1);
Seraph_Q128 predicted_x = seraph_galactic_predict(surface->cursor_x, lookahead);
Seraph_Q128 predicted_y = seraph_galactic_predict(surface->cursor_y, lookahead);

/* Use PREDICTED distance for swelling - orb responds before cursor arrives */
float dist_predicted = distance_2d(predicted_x, predicted_y, orb_x, orb_y);
float dist_current = distance_2d(cursor_x, cursor_y, orb_x, orb_y);

/* Use smaller of current and predicted for reactive swelling */
float effective_dist = (dist_predicted < dist_current) ? dist_predicted : dist_current;
```

## The Dark Seraphic Theme

The Surface uses a sophisticated dark theme inspired by seraphic aesthetics:

```c
/* Core palette */
#define SERAPH_THEME_BG        ((Seraph_Color){13, 14, 20, 255})      /* Deep void navy */
#define SERAPH_THEME_PRIMARY   ((Seraph_Color){107, 123, 142, 255})   /* Steel blue-gray */
#define SERAPH_THEME_ACCENT    ((Seraph_Color){192, 197, 206, 255})   /* Silver highlight */
#define SERAPH_THEME_GLOW      ((Seraph_Color){70, 130, 180, 255})    /* Ethereal blue glow */
#define SERAPH_THEME_LOCUS     ((Seraph_Color){147, 112, 219, 255})   /* Mystical purple */
#define SERAPH_THEME_WARNING   ((Seraph_Color){255, 170, 100, 255})   /* Amber warning */
#define SERAPH_THEME_VOID      ((Seraph_Color){40, 40, 50, 255})      /* VOID indicator */
```

## Surface Operations

### Initialization

```c
Seraph_Vbit seraph_surface_init(
    Seraph_Surface* surface,
    uint32_t width,
    uint32_t height
);

Seraph_Vbit seraph_surface_init_with_config(
    Seraph_Surface* surface,
    uint32_t width,
    uint32_t height,
    Seraph_Surface_Config config
);
```

### Orb Management

```c
/* Create an orb for a Sovereign application */
int32_t seraph_surface_create_orb(
    Seraph_Surface* surface,
    Seraph_Capability sovereign_cap,
    float orbit_distance,
    float orbit_angle
);

/* Remove an orb */
Seraph_Vbit seraph_surface_remove_orb(
    Seraph_Surface* surface,
    int32_t orb_index
);

/* Expand orb to fullscreen */
Seraph_Vbit seraph_surface_expand_orb(
    Seraph_Surface* surface,
    int32_t orb_index
);

/* Contract orb back to orbit */
Seraph_Vbit seraph_surface_contract_orb(
    Seraph_Surface* surface,
    int32_t orb_index
);
```

### Input Handling

```c
/* Update cursor with position AND velocity for anticipation */
void seraph_surface_update_cursor_with_velocity(
    Seraph_Surface* surface,
    float x,
    float y,
    float vel_x,
    float vel_y
);

/* Set cursor presence (gaze tracking, mouse in/out) */
void seraph_surface_set_cursor_present(
    Seraph_Surface* surface,
    Seraph_Vbit present
);
```

### Physics Simulation

```c
/* Run one physics step */
void seraph_surface_physics_step(
    Seraph_Surface* surface,
    Seraph_Chronon delta_chronon
);
```

The physics step handles:
1. **Anticipation**: Predict cursor position using Galactic derivatives
2. **Reactive swelling**: Orbs grow when cursor approaches (using predicted distance)
3. **Orbital mechanics**: Idle orbs slowly rotate around the Locus
4. **Magnetic attraction**: Orbs are gently pulled toward cursor
5. **State transitions**: Hover, swell, expand based on proximity

### Intent Detection

The three-phase intent model prevents accidental actions:

```c
typedef enum {
    SERAPH_INTENT_NONE = 0,         /**< No intent detected */
    SERAPH_INTENT_PREVIEW = 1,      /**< Showing potential action */
    SERAPH_INTENT_COMMIT = 2,       /**< Action committed */
    SERAPH_INTENT_UNDO = 3,         /**< Brief window to reverse */
    SERAPH_INTENT_VOID = 0xFF       /**< Invalid state */
} Seraph_Intent_Phase;
```

## SDF Rendering

The Surface uses Signed Distance Fields for resolution-independent rendering:

```c
/* Render an orb using SDF circle */
void seraph_surface_render_orb(
    Seraph_Orb* orb,
    uint32_t* framebuffer,
    uint32_t width,
    uint32_t height
);

/* Render the complete surface */
void seraph_surface_render(
    Seraph_Surface* surface,
    uint32_t* framebuffer,
    uint32_t width,
    uint32_t height
);
```

## VOID Semantics in Surface

The Surface fully embraces VOID semantics:

- Invalid orb indices return `SERAPH_VBIT_VOID`
- Operations on NULL surfaces are safe (return VOID)
- VOID orbs (`SERAPH_ORB_VOID`) are skipped in physics and rendering
- Cursor presence uses `Seraph_Vbit` for three-valued logic

## Configuration

```c
typedef struct {
    float swell_factor;             /**< How much orbs grow on approach (1.5 = 50% larger) */
    float magnetism_strength;       /**< Attraction to cursor (0.0 = none) */
    float orbital_velocity;         /**< Base orbital speed */
    float expansion_speed;          /**< How fast orbs expand/contract */
    Seraph_Q128 physics_timestep;   /**< Fixed timestep for physics */
} Seraph_Surface_Config;

#define SERAPH_SURFACE_CONFIG_DEFAULT ((Seraph_Surface_Config){ \
    .swell_factor = 1.5f,                                      \
    .magnetism_strength = 0.5f,                                \
    .orbital_velocity = 0.1f,                                  \
    .expansion_speed = 5.0f,                                   \
    .physics_timestep = {0}                                    \
})
```

## Atlas Persistence: A UI That Survives the Apocalypse

The Surface integrates with Atlas (MC27) for persistent UI state. When connected to Atlas, Orb positions are written to the Genesis transaction log and survive crashes, power loss, and reboots.

### The Philosophy

**"When you move an Orb, the universe remembers."**

Traditional UIs lose window positions on crash. SERAPH's Surface state persists forever once connected to Atlas. Move an Orb to a new position, and it stays there across restarts.

### What Persists (and What Doesn't)

**Persisted (survives apocalypse):**
- Orb positions (X, Y coordinates)
- Orbital parameters (distance, angle, velocity)
- Visual properties (base radius, colors)
- Orb state (IDLE, PERIPHERAL, etc.)
- Locus position
- Surface configuration

**NOT Persisted (recomputed from physics):**
- Velocities (Galactic tangent components)
- Animation state (glow, brightness)
- Transient focus state

This is intentional: on restart, the UI appears exactly where you left it, but **at rest**. No wild velocities from the moment of crash. Physics takes over smoothly.

### Persistent Structures

```c
/**
 * @brief Persistent Orb state - what survives a reboot
 */
typedef struct {
    uint64_t orb_id;
    Seraph_Q128 position_x;      /* Position only, no velocity */
    Seraph_Q128 position_y;
    Seraph_Q128 orbit_distance;
    Seraph_Q128 orbit_angle;
    Seraph_Q128 orbit_velocity;
    float base_radius;
    Seraph_Orb_State state;
    Seraph_Color color_base;
    Seraph_Color color_glow;
    uint64_t sovereign_cap_base;
    uint8_t _reserved[16];
} Seraph_Surface_Persistent_Orb;

/**
 * @brief Persistent Surface state - the root in Atlas
 */
typedef struct {
    uint64_t magic;              /* SERAPH_SURFACE_MAGIC for validation */
    uint64_t version;
    uint32_t width, height;
    Seraph_Q128 locus_x, locus_y;
    Seraph_Surface_Config config;
    uint32_t orb_count;
    Seraph_Chronon last_modified;
    Seraph_Surface_Persistent_Orb orbs[SERAPH_SURFACE_MAX_ORBS];
} Seraph_Surface_Persistent_State;
```

### Persistence API

```c
/* Connect Surface to Atlas for persistence */
Seraph_Vbit seraph_surface_set_atlas(
    Seraph_Surface* surface,
    Seraph_Atlas* atlas
);

/* Initialize Surface from existing Atlas state (post-apocalypse recovery) */
Seraph_Vbit seraph_surface_init_from_atlas(
    Seraph_Surface* surface,
    Seraph_Atlas* atlas
);

/* Persist a single orb's state (call after movement) */
Seraph_Vbit seraph_surface_persist_orb(
    Seraph_Surface* surface,
    int32_t orb_index
);

/* Persist entire Surface state */
Seraph_Vbit seraph_surface_persist(Seraph_Surface* surface);

/* Check if Atlas has existing Surface state */
bool seraph_surface_has_persistent_state(Seraph_Atlas* atlas);
```

### Usage Example

```c
/* Create Surface and connect to Atlas */
Seraph_Surface surface;
Seraph_Atlas atlas;

seraph_atlas_init(&atlas, "ui_state.atlas", 0);
seraph_surface_init(&surface, 1920, 1080);
seraph_surface_set_atlas(&surface, &atlas);

/* Create orbs */
int32_t orb1 = seraph_surface_create_orb(&surface, cap1, 150.0f, 0.0f);
int32_t orb2 = seraph_surface_create_orb(&surface, cap2, 150.0f, 2.094f);

/* Persist after orb movement */
seraph_surface_persist_orb(&surface, orb1);

/* ... apocalypse happens ... */

/* Later: Restore from Atlas */
Seraph_Surface restored;
Seraph_Atlas atlas2;

seraph_atlas_init(&atlas2, "ui_state.atlas", 0);  /* Same file */
seraph_surface_init_from_atlas(&restored, &atlas2);

/* restored.orbs now contains orb1 and orb2 at their exact positions */
```

### Transaction Safety

Each `seraph_surface_persist_orb()` call creates an Atlas transaction:
1. Begin transaction
2. Write orb state to persistent structure
3. Mark dirty pages
4. Commit transaction

If power is lost between steps 1-3, no data is corrupted (transaction aborts). If power is lost during step 4, either the old or new state survives (atomic commit).

## Test Coverage

42 comprehensive tests covering:
- Theme color definitions and conversions
- Surface initialization with default and custom configs
- Locus positioning and state
- Orb lifecycle (create, remove, find)
- Cursor input with velocity tracking
- Physics simulation (swelling, orbiting, attraction)
- Intent detection and cancellation
- Expansion/contraction mechanics
- SDF rendering (no crashes, correct colors)
- Null safety throughout
- **Atlas persistence** (set atlas, persist orb, persist full)
- **Apocalypse recovery** (init from atlas, position accuracy)
- **Persistence validation** (has persistent state, null safety)

## Integration with Other Components

- **Sovereign (MC10)**: Each Orb holds a capability to its underlying Sovereign
- **Glyph (MC9)**: SDF primitives used for all rendering
- **Galactic (MC5+)**: Automatic differentiation for anticipation physics
- **Capability (MC6)**: Orbs reference Sovereigns via capabilities
- **Chronon (MC7)**: Physics uses Chronon for temporal tracking
- **VOID (MC0)**: Three-valued logic throughout
- **Atlas (MC27)**: Persistent storage for UI state (Genesis transaction log)

## Summary

The Surface represents a paradigm shift in user interfaces:

1. **Physics-based**: UI elements have mass, velocity, and respond to forces
2. **Anticipatory**: Galactic numbers enable predicting user intent
3. **Gravitational**: Your attention literally attracts information
4. **Capability-secured**: Orbs can only access their authorized Sovereign
5. **VOID-aware**: Errors propagate gracefully, no crashes
6. **Apocalypse-proof**: Connected to Atlas, UI state survives power loss and crashes

This creates an interface that feels *alive* - responsive, anticipatory, and deeply connected to your attention and intent. And unlike traditional UIs, it **remembers** - your Orb positions persist forever, surviving any system failure.

## Source Files

| File | Description |
|------|-------------|
| `src/surface.c` | Surface compositor, Orb physics, Locus management |
| `src/surface_render.c` | SDF rendering, theme colors, framebuffer output |
| `src/surface_persist.c` | Atlas persistence, apocalypse recovery |
| `include/seraph/surface.h` | Surface API, Orb structures, Locus types |
