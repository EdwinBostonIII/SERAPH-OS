# MC9: The Glyph - Hyper-Dual SDF Rendering

## Plain English Explanation

### What is a Glyph?

A Glyph is SERAPH's way of storing graphics as math instead of pixels. Instead of saving "this pixel is red, this pixel is blue," a Glyph stores "a circle of radius 5 centered at (10, 10)." The computer then calculates what each pixel should look like at render time.

**Why this matters**: Traditional images become blurry when you zoom in (think of a pixelated photo). Glyphs stay perfectly sharp at ANY zoom level because they're mathematical equations, not fixed grids of dots.

### What is an SDF (Signed Distance Field)?

An SDF is a mathematical function that answers one question: "How far is point (x, y) from the shape's edge?"

- **Negative distance** = the point is INSIDE the shape
- **Positive distance** = the point is OUTSIDE the shape
- **Zero distance** = the point is exactly ON the edge

For example, a circle SDF is simply:
```
distance = distance_from_center - radius
```

If you're at the center of a circle with radius 5, your distance is `0 - 5 = -5` (5 units inside).

### Why Hyper-Dual?

SERAPH uses Galactic (Hyper-Dual) numbers for SDF evaluation. This means when you compute the distance, you also automatically get the **gradient** (which direction the surface faces) for free.

**Why this matters**: Traditional graphics need to either:
1. Compute gradients separately (slow)
2. Approximate them with multiple samples (inaccurate)

SERAPH gets exact gradients from a single evaluation, enabling perfect anti-aliasing.

### Anti-Aliasing Built Into Math

When you render a shape, edges can look "jaggy" (aliased). Traditional systems blur edges with supersampling - rendering at higher resolution and averaging. This is expensive.

SERAPH's SDFs have anti-aliasing built into the math. The SDF tells you exactly how much of each pixel is covered by the shape. No supersampling needed, yet edges are perfectly smooth.

### Boolean Operations

You can combine shapes using Boolean logic:
- **Union**: Combine two shapes (OR)
- **Intersection**: Keep only the overlapping part (AND)
- **Subtraction**: Cut one shape out of another (A AND NOT B)

These operations are simple min/max on distances:
- `union(A, B) = min(distance_A, distance_B)`
- `intersect(A, B) = max(distance_A, distance_B)`
- `subtract(A, B) = max(distance_A, -distance_B)`

### VOID Integration

If any input is VOID, the result is VOID. This means errors naturally bubble up through your rendering pipeline without explicit error handling.

Exception: `union(A, VOID) = A` because union with nothing is just the original shape.

---

## Technical Reference

### Glyph Handle Format

A `Seraph_Glyph` is a 64-bit handle with the following bit layout:

```
Bits 63-48: Arena index (16 bits) - which memory arena holds the glyph data
Bits 47-44: Kind (4 bits) - primitive type (circle, box, etc.)
Bits 43-36: Flags (8 bits) - visibility, interactivity, etc.
Bits 35-32: Transform index (4 bits) - index into transform array
Bits 31-0:  Instance ID (32 bits) - unique identifier within arena
```

**VOID Glyph**: `0xFFFFFFFFFFFFFFFF` (all ones)

### Glyph Kinds

| Value | Name | Description |
|-------|------|-------------|
| 0 | CIRCLE | Circular SDF: `d = |p - center| - radius` |
| 1 | BOX | Axis-aligned rectangle |
| 2 | ROUNDED_BOX | Rectangle with rounded corners |
| 3 | LINE | Line segment with thickness |
| 4 | RING | Annulus (donut) shape |
| 5 | TRIANGLE | Filled triangle |
| 6 | TEXT | Text string (MSDF atlas) |
| 7 | COMPOSITE | Boolean combination |
| 8 | MSDF | Multi-channel SDF texture |
| 9 | PROCEDURAL | Noise, fractals, etc. |
| 10 | BEZIER | Bezier curves |
| 11 | POLYGON | Arbitrary polygon |
| 15 | VOID | Non-existent glyph |

### Glyph Flags

| Flag | Value | Description |
|------|-------|-------------|
| VISIBLE | 0x01 | Render this glyph |
| INTERACTIVE | 0x02 | Hit-testable |
| SHADOW | 0x04 | Has shadow band |
| GLOW | 0x08 | Has glow band |
| CLIP_CHILD | 0x10 | Clips children |
| PHYSICS | 0x20 | Participates in physics |
| DIRTY | 0x40 | Needs re-evaluation |
| VOID_STATE | 0x80 | In VOID state |

### SDF Result Structure

```c
typedef struct {
    Seraph_Q128 distance;    // Signed distance to surface
    Seraph_Q128 gradient_x;  // d(dist)/dx - surface normal X
    Seraph_Q128 gradient_y;  // d(dist)/dy - surface normal Y
    Seraph_Q128 curvature;   // Second derivative for AA quality
} Seraph_SDF_Result;
```

**VOID Result**: Distance is `SERAPH_Q128_VOID`

### Primitive SDF Functions

#### Circle
```c
Seraph_SDF_Result seraph_sdf_circle(
    Seraph_Glyph_Point p,      // Query point
    Seraph_Q128 center_x,      // Circle center X
    Seraph_Q128 center_y,      // Circle center Y
    Seraph_Q128 radius         // Circle radius
);
```

Mathematical definition: `d = |p - center| - radius`

#### Box
```c
Seraph_SDF_Result seraph_sdf_box(
    Seraph_Glyph_Point p,      // Query point
    Seraph_Q128 half_width,    // Half the box width
    Seraph_Q128 half_height    // Half the box height
);
```

Box is centered at origin. Uses "fold to first quadrant" optimization.

#### Rounded Box
```c
Seraph_SDF_Result seraph_sdf_rounded_box(
    Seraph_Glyph_Point p,
    Seraph_Q128 half_width,
    Seraph_Q128 half_height,
    Seraph_Q128 corner_radius
);
```

Implemented as: shrink box by radius, expand SDF by radius.

#### Line Segment
```c
Seraph_SDF_Result seraph_sdf_line(
    Seraph_Glyph_Point p,
    Seraph_Q128 x1, Seraph_Q128 y1,  // Start point
    Seraph_Q128 x2, Seraph_Q128 y2,  // End point
    Seraph_Q128 thickness
);
```

Creates a capsule-shaped line (rounds at endpoints).

#### Ring (Annulus)
```c
Seraph_SDF_Result seraph_sdf_ring(
    Seraph_Glyph_Point p,
    Seraph_Q128 center_x,
    Seraph_Q128 center_y,
    Seraph_Q128 radius,
    Seraph_Q128 thickness
);
```

Mathematical definition: `d = ||p - center| - radius| - thickness/2`

#### Triangle
```c
Seraph_SDF_Result seraph_sdf_triangle(
    Seraph_Glyph_Point p,
    Seraph_Q128 x1, Seraph_Q128 y1,
    Seraph_Q128 x2, Seraph_Q128 y2,
    Seraph_Q128 x3, Seraph_Q128 y3
);
```

Uses winding number to determine inside/outside.

### Boolean Operations

| Function | Operation | VOID Behavior |
|----------|-----------|---------------|
| `seraph_sdf_union(a, b)` | min(a, b) | Union(A, VOID) = A |
| `seraph_sdf_intersect(a, b)` | max(a, b) | Intersect(A, VOID) = VOID |
| `seraph_sdf_subtract(a, b)` | max(a, -b) | Subtract(VOID, B) = VOID |
| `seraph_sdf_xor(a, b)` | max(min, -max) | XOR(A, VOID) = A |

### Smooth Boolean Operations

Add a blend radius `k` for soft transitions:

```c
Seraph_SDF_Result seraph_sdf_smooth_union(a, b, k);
Seraph_SDF_Result seraph_sdf_smooth_intersect(a, b, k);
Seraph_SDF_Result seraph_sdf_smooth_subtract(a, b, k);
```

Larger `k` = softer blend. `k = 0` = hard transition.

### Coverage and Anti-Aliasing

```c
Seraph_Q128 seraph_glyph_coverage(
    Seraph_SDF_Result result,
    Seraph_Q128 pixel_size
);
```

Returns coverage value [0, 1]:
- 0.0 = pixel completely outside
- 1.0 = pixel completely inside
- 0.5 = pixel on edge

Uses smoothstep for smooth transition: `3t² - 2t³`

### Hit Testing

```c
bool seraph_glyph_hit_test(Seraph_SDF_Result result);
```

Returns `true` if `distance < 0` (point is inside shape).

Same SDF used for rendering also handles collision detection.

### Distance and Normal Queries

```c
double seraph_glyph_distance(Seraph_SDF_Result result);
void seraph_glyph_normal(Seraph_SDF_Result result, double* nx, double* ny);
```

- Distance returns `INFINITY` for VOID results
- Normal is the normalized gradient vector

### Transformations

```c
// Negate: flip inside/outside
Seraph_SDF_Result seraph_sdf_negate(Seraph_SDF_Result r);

// Offset: expand (positive) or shrink (negative)
Seraph_SDF_Result seraph_sdf_offset(Seraph_SDF_Result r, Seraph_Q128 amount);

// Round: smooth corners by amount
Seraph_SDF_Result seraph_sdf_round(Seraph_SDF_Result r, Seraph_Q128 radius);
```

---

## Integration with Other SERAPH Components

### Q128 (MC5)
All distances use Q64.64 fixed-point for deterministic rendering.

### Galactic (MC5+)
Coordinates are Hyper-Dual numbers for automatic differentiation.

### VOID (MC0)
VOID propagates through all SDF operations.

### Arena (MC8)
Glyph data stored in Spectral Arenas with generation-based lifetime.

### Capability (MC6)
Glyph handles are capability-protected. Can't render what you can't access.

---

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Primitive SDF | O(1) | Single evaluation |
| Boolean op | O(1) | Simple min/max |
| Coverage | O(1) | No supersampling |
| Hit test | O(1) | Same as SDF |

No memory allocation during SDF evaluation. All operations are pure functions.

---

## Files

| File | Purpose |
|------|---------|
| `include/seraph/glyph.h` | Header with types and inline functions |
| `src/glyph.c` | SDF primitive implementations |
| `tests/test_glyph.c` | Comprehensive test suite |
| `docs/11-glyph-sdf.md` | This documentation |

---

## Example Usage

```c
// Create a point at (0.5, 0.5)
Seraph_Glyph_Point p = seraph_glyph_point_create(
    seraph_q128_from_double(0.5),
    seraph_q128_from_double(0.5)
);

// Evaluate circle SDF (center at origin, radius 1)
Seraph_SDF_Result circle = seraph_sdf_circle(
    p,
    SERAPH_Q128_ZERO,  // center_x
    SERAPH_Q128_ZERO,  // center_y
    SERAPH_Q128_ONE    // radius
);

// Check if inside
if (seraph_glyph_hit_test(circle)) {
    printf("Point is inside circle\n");
}

// Get coverage for anti-aliased rendering
double alpha = seraph_glyph_alpha(circle, 0.01);  // 0.01 pixel size
```

## Boolean Example

```c
// Create two overlapping circles
Seraph_SDF_Result c1 = seraph_sdf_circle(p, ...);
Seraph_SDF_Result c2 = seraph_sdf_circle(p, ...);

// Union: combined shape
Seraph_SDF_Result combined = seraph_sdf_union(c1, c2);

// Subtraction: c1 with c2 carved out
Seraph_SDF_Result carved = seraph_sdf_subtract(c1, c2);

// Smooth union: blended shapes
Seraph_Q128 blend_radius = seraph_q128_from_double(0.1);
Seraph_SDF_Result blended = seraph_sdf_smooth_union(c1, c2, blend_radius);
```
