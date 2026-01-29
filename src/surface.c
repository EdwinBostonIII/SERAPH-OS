/**
 * @file surface.c
 * @brief MC11: The Surface - Zero-Oversight Physics-Based UI Compositor
 *
 * Implementation of the Surface compositor. The Surface is SERAPH's
 * complete UI paradigm, featuring:
 *   - Physics-based orb interactions
 *   - Intent detection (three-phase model)
 *   - SDF-based rendering
 *   - Dark seraphic theme
 */

#include "seraph/surface.h"
#include <string.h>
#include <math.h>

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/** Orb ID generation counter */
static uint64_t orb_id_counter = 0;

/**
 * @brief Generate unique orb ID
 */
static uint64_t generate_orb_id(void) {
    orb_id_counter++;
    if (orb_id_counter == SERAPH_VOID_U64) {
        orb_id_counter = 1;  /* Skip VOID value */
    }
    return orb_id_counter;
}

/**
 * @brief Clamp value to range
 */
static float clamp_f(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/* lerp_f removed - using Galactic integration instead */

/**
 * @brief Smooth step (cubic interpolation)
 * Used for smooth SDF anti-aliasing transitions
 */
static float smoothstep_f(float edge0, float edge1, float x) {
    float t = clamp_f((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/**
 * @brief Distance between two points
 */
static float distance_2d(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrtf(dx * dx + dy * dy);
}

/**
 * @brief Get orb center position as floats
 */
static void orb_get_center(Seraph_Orb* orb, float* x, float* y) {
    /* Extract primal values from separate X and Y Galactics
     * Each axis has its own (position, velocity) pair for proper physics */
    *x = (float)seraph_q128_to_double(orb->position_x.primal);
    *y = (float)seraph_q128_to_double(orb->position_y.primal);
}

/**
 * @brief Set orb center position (preserves velocity)
 */
static void orb_set_center(Seraph_Orb* orb, float x, float y) {
    /* Only update position (primal), preserve velocity (tangent) */
    orb->position_x.primal = seraph_q128_from_double((double)x);
    orb->position_y.primal = seraph_q128_from_double((double)y);
}

/**
 * @brief Get orb current radius
 */
static float orb_get_radius(Seraph_Orb* orb) {
    return (float)seraph_q128_to_double(orb->radius.primal);
}

/**
 * @brief Set orb radius
 */
static void orb_set_radius(Seraph_Orb* orb, float r) {
    orb->radius.primal = seraph_q128_from_double((double)r);
}

/**
 * @brief Initialize orb position with zero velocity
 */
static void orb_init_position(Seraph_Orb* orb, float x, float y) {
    orb->position_x = seraph_galactic_from_pos_vel((double)x, 0.0);
    orb->position_y = seraph_galactic_from_pos_vel((double)y, 0.0);
}

/* NOTE: Velocity is automatically managed by Galactic physics
 * via seraph_galactic_integrate_accel. Direct velocity manipulation
 * functions were removed as they bypass the physics system. */

/*============================================================================
 * Surface Initialization
 *============================================================================*/

Seraph_Vbit seraph_surface_init(
    Seraph_Surface* surface,
    uint32_t width,
    uint32_t height
) {
    return seraph_surface_init_with_config(
        surface, width, height,
        SERAPH_SURFACE_CONFIG_DEFAULT
    );
}

Seraph_Vbit seraph_surface_init_with_config(
    Seraph_Surface* surface,
    uint32_t width,
    uint32_t height,
    Seraph_Surface_Config config
) {
    if (surface == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Clear all state */
    memset(surface, 0, sizeof(Seraph_Surface));

    /* Set dimensions */
    surface->width = width;
    surface->height = height;

    /* Initialize locus at center with zero velocity */
    float center_x = (float)width / 2.0f;
    float center_y = (float)height / 2.0f;

    surface->locus.position_x = seraph_galactic_from_pos_vel((double)center_x, 0.0);
    surface->locus.position_y = seraph_galactic_from_pos_vel((double)center_y, 0.0);
    surface->locus.gravity.primal = seraph_q128_from_double(1.0);
    surface->locus.active = SERAPH_VBIT_TRUE;
    surface->locus.last_update = 0  /* Initial chronon */;

    /* Initialize cursor at center with zero velocity */
    surface->cursor_x = seraph_galactic_from_pos_vel((double)center_x, 0.0);
    surface->cursor_y = seraph_galactic_from_pos_vel((double)center_y, 0.0);
    surface->cursor_present = SERAPH_VBIT_FALSE;

    /* No expanded orb */
    surface->expanded_orb_index = -1;

    /* Initialize intent state */
    surface->intent.phase = SERAPH_INTENT_NONE;
    surface->intent.source_orb = -1;
    surface->intent.target_orb = -1;
    surface->intent.proximity = 0.0f;

    /* Store configuration */
    surface->config = config;

    /* Initialize all orbs as VOID */
    for (uint32_t i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        surface->orbs[i].orb_id = SERAPH_VOID_U64;
        surface->orbs[i].state = SERAPH_ORB_VOID;
    }
    surface->orb_count = 0;

    /* Initialize temporal state */
    surface->current_chronon = 0  /* Initial chronon */;
    surface->last_physics_update = surface->current_chronon;

    /* Initialize Atlas persistence (NULL until connected) */
    surface->atlas = NULL;
    surface->persistent = NULL;

    /* Mark as initialized */
    surface->initialized = true;

    return SERAPH_VBIT_TRUE;
}

void seraph_surface_destroy(Seraph_Surface* surface) {
    if (surface == NULL) return;

    /* Clear all orbs */
    for (uint32_t i = 0; i < surface->orb_count; i++) {
        surface->orbs[i].orb_id = SERAPH_VOID_U64;
        surface->orbs[i].state = SERAPH_ORB_VOID;
    }
    surface->orb_count = 0;
    surface->initialized = false;
}

/*============================================================================
 * Orb Operations
 *============================================================================*/

int32_t seraph_surface_create_orb(
    Seraph_Surface* surface,
    Seraph_Capability sovereign_cap,
    float orbit_distance,
    float orbit_angle
) {
    if (!seraph_surface_is_valid(surface)) {
        return -1;
    }

    if (surface->orb_count >= SERAPH_SURFACE_MAX_ORBS) {
        return -1;
    }

    /* Find first empty slot */
    int32_t slot = -1;
    for (uint32_t i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        if (surface->orbs[i].state == SERAPH_ORB_VOID) {
            slot = (int32_t)i;
            break;
        }
    }

    if (slot < 0) {
        return -1;
    }

    Seraph_Orb* orb = &surface->orbs[slot];

    /* Initialize identity */
    orb->orb_id = generate_orb_id();
    orb->sovereign_cap = sovereign_cap;

    /* Calculate initial position from orbit parameters */
    float locus_x = (float)seraph_q128_to_double(surface->locus.position_x.primal);
    float locus_y = (float)seraph_q128_to_double(surface->locus.position_y.primal);

    float orb_x = locus_x + orbit_distance * cosf(orbit_angle);
    float orb_y = locus_y + orbit_distance * sinf(orbit_angle);

    /* Initialize position with zero velocity (proper 2D Galactic) */
    orb_init_position(orb, orb_x, orb_y);

    /* Initialize orbital mechanics */
    orb->orbit_distance = seraph_q128_from_double(orbit_distance);
    orb->orbit_angle = seraph_q128_from_double(orbit_angle);
    orb->orbit_velocity = seraph_q128_from_double(0.1);  /* Slow rotation */

    /* Initialize visual properties */
    float base_radius = 30.0f;  /* Default orb size */
    orb->base_radius = base_radius;
    orb_set_radius(orb, base_radius);

    orb->brightness.primal = seraph_q128_from_double(0.8);
    orb->glow.primal = seraph_q128_from_double(0.0);

    /* Initialize state */
    orb->state = SERAPH_ORB_IDLE;
    orb->visible = SERAPH_VBIT_TRUE;
    orb->focused = SERAPH_VBIT_FALSE;
    orb->notifications = 0;

    /* Apply theme colors */
    orb->color_base = SERAPH_THEME_ORB_BASE;
    orb->color_glow = SERAPH_THEME_GLOW;

    surface->orb_count++;
    return slot;
}

Seraph_Vbit seraph_surface_remove_orb(
    Seraph_Surface* surface,
    int32_t orb_index
) {
    if (!seraph_surface_is_valid(surface)) {
        return SERAPH_VBIT_VOID;
    }

    if (orb_index < 0 || orb_index >= SERAPH_SURFACE_MAX_ORBS) {
        return SERAPH_VBIT_FALSE;
    }

    Seraph_Orb* orb = &surface->orbs[orb_index];
    if (orb->state == SERAPH_ORB_VOID) {
        return SERAPH_VBIT_FALSE;  /* Already void */
    }

    /* Clear the orb */
    memset(orb, 0, sizeof(Seraph_Orb));
    orb->orb_id = SERAPH_VOID_U64;
    orb->state = SERAPH_ORB_VOID;

    surface->orb_count--;

    /* If this was the expanded orb, clear that */
    if (surface->expanded_orb_index == orb_index) {
        surface->expanded_orb_index = -1;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Orb* seraph_surface_get_orb(
    Seraph_Surface* surface,
    int32_t orb_index
) {
    if (!seraph_surface_is_valid(surface)) {
        return NULL;
    }

    if (orb_index < 0 || orb_index >= SERAPH_SURFACE_MAX_ORBS) {
        return NULL;
    }

    Seraph_Orb* orb = &surface->orbs[orb_index];
    if (orb->state == SERAPH_ORB_VOID) {
        return NULL;
    }

    return orb;
}

int32_t seraph_surface_find_orb(
    Seraph_Surface* surface,
    Seraph_Capability sovereign_cap
) {
    if (!seraph_surface_is_valid(surface)) {
        return -1;
    }

    for (int32_t i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        Seraph_Orb* orb = &surface->orbs[i];
        if (orb->state != SERAPH_ORB_VOID) {
            /* Compare capability base pointers */
            if (orb->sovereign_cap.base == sovereign_cap.base) {
                return i;
            }
        }
    }

    return -1;
}

/*============================================================================
 * Input Operations
 *============================================================================*/

void seraph_surface_update_cursor(
    Seraph_Surface* surface,
    float x,
    float y
) {
    seraph_surface_update_cursor_with_velocity(surface, x, y, 0.0f, 0.0f);
}

void seraph_surface_update_cursor_with_velocity(
    Seraph_Surface* surface,
    float x,
    float y,
    float vel_x,
    float vel_y
) {
    if (!seraph_surface_is_valid(surface)) {
        return;
    }

    /* Store cursor position AND velocity using proper 2D Galactics
     * This enables ANTICIPATION - the physics can predict where the cursor
     * is going based on velocity, allowing orbs to react before arrival */
    surface->cursor_x = seraph_galactic_from_pos_vel((double)x, (double)vel_x);
    surface->cursor_y = seraph_galactic_from_pos_vel((double)y, (double)vel_y);

    surface->cursor_present = SERAPH_VBIT_TRUE;
}

void seraph_surface_set_cursor_present(
    Seraph_Surface* surface,
    Seraph_Vbit present
) {
    if (!seraph_surface_is_valid(surface)) {
        return;
    }
    surface->cursor_present = present;
}

/*============================================================================
 * Physics Operations
 *============================================================================*/

float seraph_surface_swell_radius(
    float cursor_x,
    float cursor_y,
    float orb_x,
    float orb_y,
    float base_radius,
    float swell_factor
) {
    /*
     * radius = base_radius + swell_factor / (distance + 1)
     *
     * As distance -> 0, radius -> base_radius + swell_factor
     * As distance -> inf, radius -> base_radius
     */
    float dist = distance_2d(cursor_x, cursor_y, orb_x, orb_y);
    float swell = swell_factor / (dist + 1.0f);
    return base_radius + swell;
}

float seraph_surface_orb_distance(
    Seraph_Surface* surface,
    int32_t orb_index
) {
    if (!seraph_surface_is_valid(surface)) {
        return SERAPH_VOID_U64;  /* Very large value = VOID */
    }

    Seraph_Orb* orb = seraph_surface_get_orb(surface, orb_index);
    if (orb == NULL) {
        return SERAPH_VOID_U64;
    }

    float cursor_x = (float)seraph_q128_to_double(surface->cursor_x.primal);
    float cursor_y = (float)seraph_q128_to_double(surface->cursor_y.primal);

    float orb_x, orb_y;
    orb_get_center(orb, &orb_x, &orb_y);

    return distance_2d(cursor_x, cursor_y, orb_x, orb_y);
}

void seraph_surface_physics_step(
    Seraph_Surface* surface,
    Seraph_Chronon delta_chronon
) {
    if (!seraph_surface_is_valid(surface)) {
        return;
    }

    if (!surface->config.physics_enabled) {
        return;  /* Physics disabled (instant mode) */
    }

    /* Get time delta in seconds */
    float dt = (float)delta_chronon / 1000000.0f;  /* Assuming microseconds */
    if (dt <= 0.0f || dt > 1.0f) {
        dt = 0.016f;  /* Default to ~60fps */
    }

    Seraph_Q128 dt_q = seraph_q128_from_double((double)dt);

    /*
     * GALACTIC ANTICIPATION PHYSICS
     * =============================
     * The cursor now has SEPARATE Galactics for X and Y axes:
     *   cursor_x: (x_position, x_velocity)
     *   cursor_y: (y_position, y_velocity)
     *
     * Using seraph_galactic_predict, we anticipate where the cursor is
     * GOING, not just where it IS. This creates reactive UI: orbs respond
     * to cursor movement BEFORE the cursor actually arrives.
     */

    /* Extract cursor position from Galactics */
    float cursor_x = (float)seraph_q128_to_double(surface->cursor_x.primal);
    float cursor_y = (float)seraph_q128_to_double(surface->cursor_y.primal);
    bool cursor_active = seraph_vbit_is_true(surface->cursor_present);

    /* ANTICIPATION: Predict cursor position 100ms into the future
     * This uses the VELOCITY stored in each Galactic's tangent component */
    Seraph_Q128 lookahead = seraph_q128_from_double(0.1);
    Seraph_Q128 predicted_x_q = seraph_galactic_predict(surface->cursor_x, lookahead);
    Seraph_Q128 predicted_y_q = seraph_galactic_predict(surface->cursor_y, lookahead);
    float predicted_cursor_x = (float)seraph_q128_to_double(predicted_x_q);
    float predicted_cursor_y = (float)seraph_q128_to_double(predicted_y_q);

    /* Update each orb */
    for (int32_t i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        Seraph_Orb* orb = &surface->orbs[i];
        if (orb->state == SERAPH_ORB_VOID) {
            continue;
        }

        float orb_x, orb_y;
        orb_get_center(orb, &orb_x, &orb_y);

        /* Calculate CURRENT distance and PREDICTED distance */
        float dist_current = distance_2d(cursor_x, cursor_y, orb_x, orb_y);
        float dist_predicted = distance_2d(predicted_cursor_x, predicted_cursor_y, orb_x, orb_y);

        /*
         * REACTIVE SWELLING
         * =================
         * Use the SMALLER of current and predicted distance for swelling.
         * If the cursor is moving toward the orb, it will swell earlier.
         * If the cursor is moving away, it won't swell prematurely.
         */
        float effective_dist = (dist_predicted < dist_current) ? dist_predicted : dist_current;

        /* Update orb radius based on proximity (swelling) */
        if (cursor_active && orb->state != SERAPH_ORB_FULLSCREEN) {
            float target_radius = seraph_surface_swell_radius(
                cursor_x, cursor_y, orb_x, orb_y,
                orb->base_radius,
                surface->config.swell_factor
            );

            /* Use Galactic integration for smooth radius animation */
            float current_radius = orb_get_radius(orb);
            float radius_velocity = (target_radius - current_radius) * 5.0f;

            /* Update radius using semi-implicit Euler via Galactic */
            Seraph_Galactic radius_gal = seraph_galactic_from_pos_vel(
                (double)current_radius,
                (double)radius_velocity
            );
            Seraph_Galactic new_radius_gal = seraph_galactic_integrate(radius_gal, dt_q);
            orb_set_radius(orb, (float)seraph_q128_to_double(new_radius_gal.primal));
        }

        /* Update orbital position (slow rotation around locus) */
        if (orb->state == SERAPH_ORB_IDLE || orb->state == SERAPH_ORB_HOVER) {
            double orbit_angle = seraph_q128_to_double(orb->orbit_angle);
            double orbit_vel = seraph_q128_to_double(orb->orbit_velocity);
            double orbit_dist = seraph_q128_to_double(orb->orbit_distance);

            /* Use Galactic integration for orbit */
            Seraph_Galactic orbit_gal = seraph_galactic_from_pos_vel(orbit_angle, orbit_vel);
            Seraph_Galactic new_orbit = seraph_galactic_integrate(orbit_gal, dt_q);

            orbit_angle = seraph_q128_to_double(new_orbit.primal);
            if (orbit_angle > 2.0 * 3.14159265359) {
                orbit_angle -= 2.0 * 3.14159265359;
            }
            orb->orbit_angle = seraph_q128_from_double(orbit_angle);

            /* Update position from orbit (locus uses separate X/Y Galactics) */
            float locus_x = (float)seraph_q128_to_double(surface->locus.position_x.primal);
            float locus_y = (float)seraph_q128_to_double(surface->locus.position_y.primal);

            float new_x = locus_x + (float)orbit_dist * cosf((float)orbit_angle);
            float new_y = locus_y + (float)orbit_dist * sinf((float)orbit_angle);

            orb_set_center(orb, new_x, new_y);
        }

        /*
         * MAGNETIC ATTRACTION with Galactic physics
         * ==========================================
         * Compute attraction force and apply as acceleration.
         * Each axis (X and Y) has its own Galactic with position and velocity,
         * so we apply seraph_galactic_integrate_accel SEPARATELY to each axis.
         */
        if (cursor_active && surface->config.magnetism_strength > 0.0f) {
            float attraction = surface->config.magnetism_strength / (effective_dist + 10.0f);
            float dx = cursor_x - orb_x;
            float dy = cursor_y - orb_y;
            float len = sqrtf(dx * dx + dy * dy) + 0.001f;
            dx /= len;
            dy /= len;

            /* Compute acceleration toward cursor for BOTH axes */
            float accel_x = dx * attraction * 10.0f;
            float accel_y = dy * attraction * 10.0f;

            /* Apply acceleration to X axis using Galactic integration */
            Seraph_Q128 accel_x_q = seraph_q128_from_double((double)accel_x);
            orb->position_x = seraph_galactic_integrate_accel(orb->position_x, accel_x_q, dt_q);

            /* Apply acceleration to Y axis using Galactic integration */
            Seraph_Q128 accel_y_q = seraph_q128_from_double((double)accel_y);
            orb->position_y = seraph_galactic_integrate_accel(orb->position_y, accel_y_q, dt_q);

            /* Extract new position from both axes */
            orb_x = (float)seraph_q128_to_double(orb->position_x.primal);
            orb_y = (float)seraph_q128_to_double(orb->position_y.primal);
        }

        /*
         * VELOCITY DAMPING (friction)
         * ============================
         * Apply damping to prevent infinite oscillation.
         * Velocity is stored in the tangent component of each Galactic.
         * damping_factor of 0.95 means we retain 95% of velocity each frame
         * (equivalent to ~5% friction per frame).
         *
         * damping = 1.0 - damping_factor * dt  (time-corrected)
         */
        if (surface->config.damping_factor > 0.0f) {
            float damping = 1.0f - (1.0f - surface->config.damping_factor) * dt * 60.0f;
            if (damping < 0.0f) damping = 0.0f;
            if (damping > 1.0f) damping = 1.0f;

            /* Apply damping to velocity (tangent component) */
            orb->position_x.tangent = seraph_q128_mul(orb->position_x.tangent,
                                                       seraph_q128_from_double((double)damping));
            orb->position_y.tangent = seraph_q128_mul(orb->position_y.tangent,
                                                       seraph_q128_from_double((double)damping));
        }

        /*
         * STATE TRANSITIONS based on PREDICTED proximity
         * ================================================
         * Use predicted distance for state changes - this creates
         * anticipatory state transitions (orb starts hovering before
         * cursor arrives if cursor is moving quickly toward it).
         */
        if (cursor_active) {
            float radius = orb_get_radius(orb);
            if (effective_dist < radius * 1.5f) {
                if (orb->state == SERAPH_ORB_IDLE) {
                    orb->state = SERAPH_ORB_HOVER;
                    orb->focused = SERAPH_VBIT_TRUE;
                }
                if (effective_dist < radius * 0.5f) {
                    orb->state = SERAPH_ORB_SWELLING;
                }
            } else {
                if (orb->state == SERAPH_ORB_HOVER || orb->state == SERAPH_ORB_SWELLING) {
                    orb->state = SERAPH_ORB_IDLE;
                    orb->focused = SERAPH_VBIT_FALSE;
                }
            }
        }

        /* Update brightness using Galactic smoothing */
        float target_brightness = 0.7f;
        if (seraph_vbit_is_true(orb->focused)) {
            target_brightness = 1.0f;
        }
        float current_brightness = (float)seraph_q128_to_double(orb->brightness.primal);
        float brightness_vel = (target_brightness - current_brightness) * 3.0f;

        Seraph_Galactic brightness_gal = seraph_galactic_from_pos_vel(
            (double)current_brightness,
            (double)brightness_vel
        );
        Seraph_Galactic new_brightness = seraph_galactic_integrate(brightness_gal, dt_q);
        orb->brightness.primal = new_brightness.primal;
    }

    surface->last_physics_update = surface->current_chronon;
}

/*============================================================================
 * Intent Detection
 *============================================================================*/

int32_t seraph_surface_detect_intent(Seraph_Surface* surface) {
    if (!seraph_surface_is_valid(surface)) {
        return -1;
    }

    if (!seraph_vbit_is_true(surface->cursor_present)) {
        return -1;
    }

    /* Find the closest orb */
    int32_t closest_orb = -1;
    float closest_dist = 1e10f;

    for (int32_t i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        Seraph_Orb* orb = &surface->orbs[i];
        if (orb->state == SERAPH_ORB_VOID || orb->state == SERAPH_ORB_FULLSCREEN) {
            continue;
        }

        float dist = seraph_surface_orb_distance(surface, i);
        float radius = orb_get_radius(orb);

        /* Intent is detected when cursor is within orb radius */
        if (dist < radius && dist < closest_dist) {
            closest_dist = dist;
            closest_orb = i;
        }
    }

    return closest_orb;
}

Seraph_Intent_State seraph_surface_get_intent(Seraph_Surface* surface) {
    if (!seraph_surface_is_valid(surface)) {
        return (Seraph_Intent_State){
            .phase = SERAPH_INTENT_VOID,
            .source_orb = -1,
            .target_orb = -1
        };
    }
    return surface->intent;
}

void seraph_surface_cancel_intent(Seraph_Surface* surface) {
    if (!seraph_surface_is_valid(surface)) {
        return;
    }
    surface->intent.phase = SERAPH_INTENT_NONE;
    surface->intent.source_orb = -1;
    surface->intent.target_orb = -1;
    surface->intent.proximity = 0.0f;
}

/*============================================================================
 * Expansion/Contraction
 *============================================================================*/

Seraph_Vbit seraph_surface_expand_orb(
    Seraph_Surface* surface,
    int32_t orb_index
) {
    if (!seraph_surface_is_valid(surface)) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Orb* orb = seraph_surface_get_orb(surface, orb_index);
    if (orb == NULL) {
        return SERAPH_VBIT_FALSE;
    }

    if (orb->state == SERAPH_ORB_FULLSCREEN) {
        return SERAPH_VBIT_FALSE;  /* Already expanded */
    }

    /* Contract any currently expanded orb */
    if (surface->expanded_orb_index >= 0 &&
        surface->expanded_orb_index != orb_index) {
        seraph_surface_contract_current(surface);
    }

    /* Start expansion animation */
    orb->state = SERAPH_ORB_EXPANDING;
    surface->expanded_orb_index = orb_index;

    /* For now, immediately go to fullscreen */
    orb->state = SERAPH_ORB_FULLSCREEN;

    /* Move other orbs to peripheral state */
    for (int32_t i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        if (i != orb_index && surface->orbs[i].state != SERAPH_ORB_VOID) {
            surface->orbs[i].state = SERAPH_ORB_PERIPHERAL;
        }
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_surface_contract_current(Seraph_Surface* surface) {
    if (!seraph_surface_is_valid(surface)) {
        return SERAPH_VBIT_VOID;
    }

    if (surface->expanded_orb_index < 0) {
        return SERAPH_VBIT_FALSE;  /* Nothing to contract */
    }

    Seraph_Orb* orb = seraph_surface_get_orb(surface, surface->expanded_orb_index);
    if (orb == NULL) {
        surface->expanded_orb_index = -1;
        return SERAPH_VBIT_FALSE;
    }

    /* Start contraction animation */
    orb->state = SERAPH_ORB_CONTRACTING;

    /* For now, immediately go to idle */
    orb->state = SERAPH_ORB_IDLE;
    orb_set_radius(orb, orb->base_radius);

    /* Restore other orbs from peripheral state */
    for (int32_t i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        if (surface->orbs[i].state == SERAPH_ORB_PERIPHERAL) {
            surface->orbs[i].state = SERAPH_ORB_IDLE;
        }
    }

    surface->expanded_orb_index = -1;
    return SERAPH_VBIT_TRUE;
}

bool seraph_surface_is_orb_expanded(
    Seraph_Surface* surface,
    int32_t orb_index
) {
    if (!seraph_surface_is_valid(surface)) {
        return false;
    }
    return surface->expanded_orb_index == orb_index;
}

/*============================================================================
 * Rendering
 *============================================================================*/

/**
 * @brief Render a filled circle using SDF
 */
static void render_circle_sdf(
    uint32_t* framebuffer,
    uint32_t fb_width,
    uint32_t fb_height,
    float cx,
    float cy,
    float radius,
    Seraph_Color fill_color,
    Seraph_Color glow_color,
    float glow_amount
) {
    /* Compute bounding box with glow margin */
    float margin = radius * 2.0f;
    int32_t x0 = (int32_t)(cx - margin);
    int32_t y0 = (int32_t)(cy - margin);
    int32_t x1 = (int32_t)(cx + margin);
    int32_t y1 = (int32_t)(cy + margin);

    /* Clamp to framebuffer */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int32_t)fb_width) x1 = (int32_t)fb_width - 1;
    if (y1 >= (int32_t)fb_height) y1 = (int32_t)fb_height - 1;

    for (int32_t y = y0; y <= y1; y++) {
        for (int32_t x = x0; x <= x1; x++) {
            /* SDF: distance from circle */
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float dist = distance_2d(px, py, cx, cy) - radius;

            /* Inside the circle */
            if (dist < 0.0f) {
                framebuffer[y * fb_width + x] = seraph_color_to_u32(fill_color);
            }
            /* Anti-aliased edge using smoothstep for crisp but smooth edges */
            else if (dist < 1.5f) {
                float alpha = 1.0f - smoothstep_f(0.0f, 1.5f, dist);
                Seraph_Color bg = {
                    (framebuffer[y * fb_width + x] >> 24) & 0xFF,
                    (framebuffer[y * fb_width + x] >> 16) & 0xFF,
                    (framebuffer[y * fb_width + x] >> 8) & 0xFF,
                    255
                };
                Seraph_Color blended = seraph_color_lerp(bg, fill_color, alpha);
                framebuffer[y * fb_width + x] = seraph_color_to_u32(blended);
            }
            /* Glow region */
            else if (glow_amount > 0.0f && dist < radius) {
                float glow_fade = 1.0f - (dist / radius);
                glow_fade = glow_fade * glow_fade * glow_amount;
                if (glow_fade > 0.01f) {
                    Seraph_Color bg = {
                        (framebuffer[y * fb_width + x] >> 24) & 0xFF,
                        (framebuffer[y * fb_width + x] >> 16) & 0xFF,
                        (framebuffer[y * fb_width + x] >> 8) & 0xFF,
                        255
                    };
                    Seraph_Color blended = seraph_color_lerp(bg, glow_color, glow_fade);
                    framebuffer[y * fb_width + x] = seraph_color_to_u32(blended);
                }
            }
        }
    }
}

void seraph_surface_render_locus(
    Seraph_Locus* locus,
    uint32_t* framebuffer,
    uint32_t width,
    uint32_t height
) {
    if (locus == NULL || framebuffer == NULL) {
        return;
    }

    float cx = (float)seraph_q128_to_double(locus->position_x.primal);
    float cy = (float)seraph_q128_to_double(locus->position_y.primal);

    /* Render a subtle locus indicator */
    render_circle_sdf(
        framebuffer, width, height,
        cx, cy, 5.0f,
        SERAPH_THEME_LOCUS,
        SERAPH_THEME_GLOW,
        0.3f
    );
}

void seraph_surface_render_orb(
    Seraph_Orb* orb,
    uint32_t* framebuffer,
    uint32_t width,
    uint32_t height,
    float center_x,
    float center_y
) {
    if (orb == NULL || framebuffer == NULL) {
        return;
    }

    if (orb->state == SERAPH_ORB_VOID) {
        return;
    }

    float radius = orb_get_radius(orb);
    float brightness = (float)seraph_q128_to_double(orb->brightness.primal);
    float glow = (float)seraph_q128_to_double(orb->glow.primal);

    /* Determine color based on state */
    Seraph_Color fill = orb->color_base;
    if (seraph_vbit_is_true(orb->focused)) {
        fill = SERAPH_THEME_ORB_HOVER;
    }
    if (orb->state == SERAPH_ORB_SWELLING) {
        fill = SERAPH_THEME_ORB_ACTIVE;
    }

    /* Apply brightness */
    fill.r = (uint8_t)(fill.r * brightness);
    fill.g = (uint8_t)(fill.g * brightness);
    fill.b = (uint8_t)(fill.b * brightness);

    /* Add glow for notifications */
    if (orb->notifications > 0) {
        glow = 0.5f;
    }

    render_circle_sdf(
        framebuffer, width, height,
        center_x, center_y, radius,
        fill, orb->color_glow, glow
    );
}

void seraph_surface_render(
    Seraph_Surface* surface,
    uint32_t* framebuffer,
    uint32_t width,
    uint32_t height
) {
    if (!seraph_surface_is_valid(surface) || framebuffer == NULL) {
        return;
    }

    /* Fill background with theme color */
    uint32_t bg = seraph_color_to_u32(SERAPH_THEME_BACKGROUND);
    for (uint32_t i = 0; i < width * height; i++) {
        framebuffer[i] = bg;
    }

    /* Render the locus */
    seraph_surface_render_locus(&surface->locus, framebuffer, width, height);

    /* Render all orbs (back to front, peripheral first) */
    /* First pass: peripheral orbs */
    for (int32_t i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        Seraph_Orb* orb = &surface->orbs[i];
        if (orb->state == SERAPH_ORB_PERIPHERAL) {
            float orb_x, orb_y;
            orb_get_center(orb, &orb_x, &orb_y);
            seraph_surface_render_orb(orb, framebuffer, width, height, orb_x, orb_y);
        }
    }

    /* Second pass: normal orbs */
    for (int32_t i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        Seraph_Orb* orb = &surface->orbs[i];
        if (orb->state != SERAPH_ORB_VOID &&
            orb->state != SERAPH_ORB_PERIPHERAL &&
            orb->state != SERAPH_ORB_FULLSCREEN) {
            float orb_x, orb_y;
            orb_get_center(orb, &orb_x, &orb_y);
            seraph_surface_render_orb(orb, framebuffer, width, height, orb_x, orb_y);
        }
    }

    /* Third pass: fullscreen orb (if any) */
    if (surface->expanded_orb_index >= 0) {
        Seraph_Orb* orb = &surface->orbs[surface->expanded_orb_index];
        if (orb->state == SERAPH_ORB_FULLSCREEN) {
            /* For fullscreen, fill the entire screen with orb color */
            uint32_t fill = seraph_color_to_u32(orb->color_base);
            for (uint32_t i = 0; i < width * height; i++) {
                framebuffer[i] = fill;
            }
        }
    }
}

/*============================================================================
 * Atlas Persistence Operations
 *============================================================================
 *
 * "A UI that survives the apocalypse."
 *
 * When connected to Atlas, the Surface writes orb positions to persistent
 * storage via the Genesis transaction log. On restart, the Surface
 * reconstructs itself from Atlas, exactly where it left off.
 */

/**
 * @brief Sync runtime orb state to persistent orb structure
 *
 * Extracts primal (position) values from Galactics and stores them
 * in the persistent structure. Velocities (tangent) are NOT persisted.
 */
static void sync_orb_to_persistent(
    Seraph_Orb* orb,
    Seraph_Surface_Persistent_Orb* persistent
) {
    persistent->orb_id = orb->orb_id;

    /* Extract primal positions only (no velocity) */
    persistent->position_x = orb->position_x.primal;
    persistent->position_y = orb->position_y.primal;

    /* Orbital parameters */
    persistent->orbit_distance = orb->orbit_distance;
    persistent->orbit_angle = orb->orbit_angle;
    persistent->orbit_velocity = orb->orbit_velocity;

    /* Visual properties */
    persistent->base_radius = orb->base_radius;
    persistent->state = orb->state;
    persistent->color_base = orb->color_base;
    persistent->color_glow = orb->color_glow;

    /* Sovereign capability base for re-association (cast pointer to uint64_t) */
    persistent->sovereign_cap_base = (uint64_t)(uintptr_t)orb->sovereign_cap.base;
}

/**
 * @brief Sync persistent orb structure to runtime orb state
 *
 * Reconstructs Galactic (position, velocity) from primal values.
 * Velocities are set to zero - physics starts from rest.
 */
static void sync_orb_from_persistent(
    Seraph_Surface_Persistent_Orb* persistent,
    Seraph_Orb* orb
) {
    orb->orb_id = persistent->orb_id;

    /* Initialize positions with zero velocity (tangent = 0) */
    orb->position_x = seraph_galactic_from_q128(persistent->position_x);
    orb->position_y = seraph_galactic_from_q128(persistent->position_y);

    /* Orbital parameters */
    orb->orbit_distance = persistent->orbit_distance;
    orb->orbit_angle = persistent->orbit_angle;
    orb->orbit_velocity = persistent->orbit_velocity;

    /* Visual properties */
    orb->base_radius = persistent->base_radius;
    orb->state = persistent->state;
    orb->color_base = persistent->color_base;
    orb->color_glow = persistent->color_glow;

    /* Initialize transient properties with zero velocity */
    orb->radius = seraph_galactic_from_q128(seraph_q128_from_double(persistent->base_radius));
    orb->brightness = seraph_galactic_from_q128(seraph_q128_from_double(0.8));
    orb->glow = seraph_galactic_from_q128(seraph_q128_from_double(0.0));
    orb->visible = SERAPH_VBIT_TRUE;
    orb->focused = SERAPH_VBIT_FALSE;
    orb->notifications = 0;

    /* Reconstruct capability with base address only (cast uint64_t back to pointer) */
    orb->sovereign_cap.base = (void*)(uintptr_t)persistent->sovereign_cap_base;
    orb->sovereign_cap.length = 0;  /* Unknown - will be re-associated */
    orb->sovereign_cap.permissions = 0;
    orb->sovereign_cap.generation = 0;
}

Seraph_Vbit seraph_surface_set_atlas(
    Seraph_Surface* surface,
    Seraph_Atlas* atlas
) {
    if (surface == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (atlas == NULL || !seraph_atlas_is_valid(atlas)) {
        return SERAPH_VBIT_VOID;
    }

    /* Connect to Atlas */
    surface->atlas = atlas;

    /* Check if persistent state already exists */
    Seraph_Surface_Persistent_State* existing = seraph_surface_get_persistent_state(atlas);
    if (existing != NULL) {
        surface->persistent = existing;
    } else {
        /* Allocate new persistent state in Atlas */
        size_t state_size = sizeof(Seraph_Surface_Persistent_State);
        Seraph_Surface_Persistent_State* state =
            (Seraph_Surface_Persistent_State*)seraph_atlas_calloc(atlas, state_size);

        if (state == NULL) {
            surface->atlas = NULL;
            return SERAPH_VBIT_VOID;
        }

        /* Initialize persistent state */
        state->magic = SERAPH_SURFACE_MAGIC;
        state->version = SERAPH_SURFACE_VERSION;
        state->width = surface->width;
        state->height = surface->height;
        state->locus_x = surface->locus.position_x.primal;
        state->locus_y = surface->locus.position_y.primal;
        state->config = surface->config;
        state->orb_count = 0;
        state->last_modified = surface->current_chronon;

        /* Set as Atlas root */
        seraph_atlas_set_root(atlas, state);

        /* Sync to disk */
        seraph_atlas_sync(atlas);

        surface->persistent = state;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_surface_init_from_atlas(
    Seraph_Surface* surface,
    Seraph_Atlas* atlas
) {
    if (surface == NULL || atlas == NULL) {
        return SERAPH_VBIT_VOID;
    }

    if (!seraph_atlas_is_valid(atlas)) {
        return SERAPH_VBIT_VOID;
    }

    /* Get persistent state */
    Seraph_Surface_Persistent_State* state = seraph_surface_get_persistent_state(atlas);
    if (state == NULL) {
        return SERAPH_VBIT_FALSE;  /* No state exists */
    }

    /* Validate magic number */
    if (state->magic != SERAPH_SURFACE_MAGIC) {
        return SERAPH_VBIT_FALSE;  /* Corrupt or invalid */
    }

    /* Initialize surface with config from persistent state */
    Seraph_Vbit result = seraph_surface_init_with_config(
        surface,
        state->width,
        state->height,
        state->config
    );

    if (!seraph_vbit_is_true(result)) {
        return SERAPH_VBIT_VOID;
    }

    /* Restore locus position (zero velocity) */
    surface->locus.position_x = seraph_galactic_from_q128(state->locus_x);
    surface->locus.position_y = seraph_galactic_from_q128(state->locus_y);

    /* Connect to Atlas */
    surface->atlas = atlas;
    surface->persistent = state;

    /* Restore all orbs */
    for (uint32_t i = 0; i < state->orb_count && i < SERAPH_SURFACE_MAX_ORBS; i++) {
        Seraph_Surface_Persistent_Orb* p_orb = &state->orbs[i];

        /* Skip VOID orbs */
        if (p_orb->orb_id == SERAPH_VOID_U64) {
            continue;
        }

        /* Find empty slot */
        int32_t slot = -1;
        for (uint32_t j = 0; j < SERAPH_SURFACE_MAX_ORBS; j++) {
            if (surface->orbs[j].state == SERAPH_ORB_VOID) {
                slot = (int32_t)j;
                break;
            }
        }

        if (slot < 0) {
            break;  /* No more slots */
        }

        /* Restore orb from persistent state */
        sync_orb_from_persistent(p_orb, &surface->orbs[slot]);
        surface->orb_count++;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_surface_persist_orb(
    Seraph_Surface* surface,
    int32_t orb_index
) {
    if (!seraph_surface_is_valid(surface)) {
        return SERAPH_VBIT_VOID;
    }

    if (surface->atlas == NULL || surface->persistent == NULL) {
        return SERAPH_VBIT_VOID;  /* Not connected to Atlas */
    }

    Seraph_Orb* orb = seraph_surface_get_orb(surface, orb_index);
    if (orb == NULL) {
        return SERAPH_VBIT_FALSE;
    }

    /* Start transaction */
    Seraph_Atlas_Transaction* tx = seraph_atlas_begin(surface->atlas);
    if (tx == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Find or allocate slot in persistent state */
    int32_t p_slot = -1;
    for (uint32_t i = 0; i < surface->persistent->orb_count; i++) {
        if (surface->persistent->orbs[i].orb_id == orb->orb_id) {
            p_slot = (int32_t)i;
            break;
        }
    }

    /* If not found, use next slot */
    if (p_slot < 0) {
        if (surface->persistent->orb_count >= SERAPH_SURFACE_MAX_ORBS) {
            seraph_atlas_abort(surface->atlas, tx);
            return SERAPH_VBIT_FALSE;  /* No room */
        }
        p_slot = (int32_t)surface->persistent->orb_count;
        surface->persistent->orb_count++;
    }

    /* Sync orb to persistent structure */
    sync_orb_to_persistent(orb, &surface->persistent->orbs[p_slot]);

    /* Mark dirty */
    seraph_atlas_tx_mark_dirty(tx, &surface->persistent->orbs[p_slot],
                               sizeof(Seraph_Surface_Persistent_Orb));
    seraph_atlas_tx_mark_dirty(tx, &surface->persistent->orb_count,
                               sizeof(surface->persistent->orb_count));

    /* Update last modified */
    surface->persistent->last_modified = surface->current_chronon;
    seraph_atlas_tx_mark_dirty(tx, &surface->persistent->last_modified,
                               sizeof(surface->persistent->last_modified));

    /* Commit transaction - this is the "apocalypse-proof" moment */
    Seraph_Vbit commit_result = seraph_atlas_commit(surface->atlas, tx);
    if (!seraph_vbit_is_true(commit_result)) {
        seraph_atlas_abort(surface->atlas, tx);
        return SERAPH_VBIT_VOID;
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_surface_persist(Seraph_Surface* surface) {
    if (!seraph_surface_is_valid(surface)) {
        return SERAPH_VBIT_VOID;
    }

    if (surface->atlas == NULL || surface->persistent == NULL) {
        return SERAPH_VBIT_VOID;  /* Not connected to Atlas */
    }

    /* Start transaction */
    Seraph_Atlas_Transaction* tx = seraph_atlas_begin(surface->atlas);
    if (tx == NULL) {
        return SERAPH_VBIT_VOID;
    }

    /* Update header fields */
    surface->persistent->width = surface->width;
    surface->persistent->height = surface->height;
    surface->persistent->locus_x = surface->locus.position_x.primal;
    surface->persistent->locus_y = surface->locus.position_y.primal;
    surface->persistent->config = surface->config;
    surface->persistent->last_modified = surface->current_chronon;

    /* Clear all persistent orbs and re-sync */
    surface->persistent->orb_count = 0;

    for (int32_t i = 0; i < SERAPH_SURFACE_MAX_ORBS; i++) {
        Seraph_Orb* orb = &surface->orbs[i];
        if (orb->state != SERAPH_ORB_VOID) {
            sync_orb_to_persistent(orb, &surface->persistent->orbs[surface->persistent->orb_count]);
            surface->persistent->orb_count++;
        }
    }

    /* Mark entire state dirty */
    seraph_atlas_tx_mark_dirty(tx, surface->persistent,
                               sizeof(Seraph_Surface_Persistent_State));

    /* Commit transaction */
    Seraph_Vbit commit_result = seraph_atlas_commit(surface->atlas, tx);
    if (!seraph_vbit_is_true(commit_result)) {
        seraph_atlas_abort(surface->atlas, tx);
        return SERAPH_VBIT_VOID;
    }

    return SERAPH_VBIT_TRUE;
}

bool seraph_surface_has_persistent_state(Seraph_Atlas* atlas) {
    if (atlas == NULL || !seraph_atlas_is_valid(atlas)) {
        return false;
    }

    void* root = seraph_atlas_get_root(atlas);
    if (root == NULL) {
        return false;
    }

    Seraph_Surface_Persistent_State* state = (Seraph_Surface_Persistent_State*)root;
    return state->magic == SERAPH_SURFACE_MAGIC;
}

Seraph_Surface_Persistent_State* seraph_surface_get_persistent_state(
    Seraph_Atlas* atlas
) {
    if (!seraph_surface_has_persistent_state(atlas)) {
        return NULL;
    }

    return (Seraph_Surface_Persistent_State*)seraph_atlas_get_root(atlas);
}
