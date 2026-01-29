# MC5+: Galactic Numbers (Hyper-Dual Automatic Differentiation)

## What are Galactic Numbers? (Plain English)

A Galactic number is a number that "remembers" its derivative. When you compute `y = sin(x)`, a Galactic number gives you BOTH:
- The value: `y = sin(x)`
- The derivative: `dy/dx = cos(x)`

This is **automatic differentiation** - calculus done by the computer, for free, at every computation.

**Why "Galactic"?** Because these numbers carry more information than regular numbers - they know not just their value but how they change. They're bigger (256 bits = 2× Q128) and more powerful.

## Why Automatic Differentiation?

Manual derivatives are:
- Error-prone (easy to make mistakes)
- Tedious (must be redone for every formula change)
- Separate from computation (derivatives stored elsewhere)

Numeric derivatives (finite differences) are:
- Approximate (subject to rounding errors)
- Slow (requires extra evaluations)
- Unstable (step size must be chosen carefully)

Automatic differentiation is:
- Exact to machine precision
- Computed alongside the value
- No extra work (same code computes both)

## The Dual Number Concept

A dual number has the form: **a + bε** where ε² = 0 (but ε ≠ 0).

Think of ε as an "infinitesimal" - a number so small that its square is zero, but it's not itself zero.

**Example**: Computing f(x) = x² at x = 3

```
f(3 + ε) = (3 + ε)²
         = 9 + 6ε + ε²
         = 9 + 6ε     (since ε² = 0)
```

The result is **9 + 6ε**:
- Primal (value): 9 = f(3) ✓
- Tangent (derivative): 6 = f'(3) = 2×3 ✓

## Galactic Structure

A Galactic number in SERAPH contains:
- **Primal**: Q128 value (the number itself)
- **Tangent**: Q128 value (the derivative)

```c
typedef struct {
    Seraph_Q128 primal;   // The value
    Seraph_Q128 tangent;  // The derivative (∂/∂x)
} Seraph_Galactic;
```

Total size: 256 bits (32 bytes)

## Arithmetic Rules

### Addition/Subtraction
```
(a + a'ε) + (b + b'ε) = (a + b) + (a' + b')ε
(a + a'ε) - (b + b'ε) = (a - b) + (a' - b')ε
```

Derivatives add/subtract.

### Multiplication (Product Rule)
```
(a + a'ε) × (b + b'ε) = ab + (a'b + ab')ε
```

This IS the product rule: (fg)' = f'g + fg'

### Division (Quotient Rule)
```
(a + a'ε) / (b + b'ε) = (a/b) + ((a'b - ab')/b²)ε
```

This IS the quotient rule: (f/g)' = (f'g - fg')/g²

### Power Rule
```
(a + a'ε)^n = a^n + (n × a^(n-1) × a')ε
```

### Chain Rule (Composition)
```
f(g(x + ε)) = f(g(x)) + f'(g(x)) × g'(x) × ε
```

This happens automatically when you compose functions!

## Transcendental Derivatives

| Function | Derivative (tangent transform) |
|----------|-------------------------------|
| sin(a + a'ε) | sin(a) + a'cos(a)ε |
| cos(a + a'ε) | cos(a) - a'sin(a)ε |
| exp(a + a'ε) | exp(a) + a'exp(a)ε |
| ln(a + a'ε) | ln(a) + (a'/a)ε |
| sqrt(a + a'ε) | sqrt(a) + (a'/(2×sqrt(a)))ε |

## Usage Example

```c
// Define x as independent variable
Seraph_Galactic x = seraph_galactic_variable(3.0);  // x = 3, dx/dx = 1

// Compute f(x) = x² + 2x + 1
Seraph_Galactic x2 = seraph_galactic_mul(x, x);           // x²
Seraph_Galactic two_x = seraph_galactic_scale(x, 2.0);    // 2x
Seraph_Galactic sum = seraph_galactic_add(x2, two_x);     // x² + 2x
Seraph_Galactic f = seraph_galactic_add_scalar(sum, 1.0); // x² + 2x + 1

// Extract results
double value = seraph_galactic_primal_to_double(f);      // f(3) = 9 + 6 + 1 = 16
double derivative = seraph_galactic_tangent_to_double(f); // f'(3) = 2×3 + 2 = 8
```

## Type Promotion Hierarchy

```
Scalar (Q128) → Dual (Galactic)
```

When a Q128 is used with a Galactic, it's automatically promoted to a Galactic with tangent = 0 (constant).

## Creating Galactic Numbers

### As a Variable (tangent = 1)
```c
Seraph_Galactic x = seraph_galactic_variable(val);  // d(x)/dx = 1
```

### As a Constant (tangent = 0)
```c
Seraph_Galactic c = seraph_galactic_constant(val);  // d(c)/dx = 0
```

### From Q128 Values
```c
Seraph_Galactic g = seraph_galactic_create(primal, tangent);
```

## VOID in Galactic Numbers

A Galactic is VOID if either component is VOID:
- VOID primal = VOID Galactic (value unknown)
- VOID tangent = VOID Galactic (derivative unknown)

Operations that would produce VOID in either component produce a fully VOID Galactic.

## Use Cases

### 1. Physics Simulation
Compute position AND velocity simultaneously:
```c
// Position is a function of time
Seraph_Galactic t = seraph_galactic_variable(current_time);
Seraph_Galactic position = compute_trajectory(t);

// Get both position and velocity (derivative of position)
Q128 pos = seraph_galactic_primal(position);
Q128 vel = seraph_galactic_tangent(position);  // Velocity = d(pos)/dt
```

### 2. Optimization (Newton's Method)
```c
// Find root of f(x) = 0 using Newton's method
while (!converged) {
    Seraph_Galactic fx = f(seraph_galactic_variable(x));
    Q128 value = seraph_galactic_primal(fx);
    Q128 deriv = seraph_galactic_tangent(fx);

    x = x - value / deriv;  // Newton step
}
```

### 3. Machine Learning (Gradient Descent)
```c
// Compute loss AND gradient in one pass
Seraph_Galactic loss = compute_loss(weights_as_variables);
Q128 loss_value = seraph_galactic_primal(loss);
Q128 gradient = seraph_galactic_tangent(loss);

weights = weights - learning_rate * gradient;
```

### 4. Graphics (Smooth Animation)
```c
// Interpolate with velocity
Seraph_Galactic t = seraph_galactic_variable(animation_time);
Seraph_Galactic position = ease_in_out(start, end, t);

Q128 current_pos = seraph_galactic_primal(position);
Q128 velocity = seraph_galactic_tangent(position);  // For motion blur
```

## API Summary

### Creation
```c
Seraph_Galactic seraph_galactic_variable(double val);
Seraph_Galactic seraph_galactic_constant(double val);
Seraph_Galactic seraph_galactic_create(Seraph_Q128 primal, Seraph_Q128 tangent);
```

### Arithmetic
```c
Seraph_Galactic seraph_galactic_add(Seraph_Galactic a, Seraph_Galactic b);
Seraph_Galactic seraph_galactic_sub(Seraph_Galactic a, Seraph_Galactic b);
Seraph_Galactic seraph_galactic_mul(Seraph_Galactic a, Seraph_Galactic b);
Seraph_Galactic seraph_galactic_div(Seraph_Galactic a, Seraph_Galactic b);
Seraph_Galactic seraph_galactic_neg(Seraph_Galactic x);
```

### Transcendentals
```c
Seraph_Galactic seraph_galactic_sqrt(Seraph_Galactic x);
Seraph_Galactic seraph_galactic_sin(Seraph_Galactic x);
Seraph_Galactic seraph_galactic_cos(Seraph_Galactic x);
Seraph_Galactic seraph_galactic_exp(Seraph_Galactic x);
Seraph_Galactic seraph_galactic_ln(Seraph_Galactic x);
Seraph_Galactic seraph_galactic_pow(Seraph_Galactic base, Seraph_Galactic exp);
```

### Extraction
```c
Seraph_Q128 seraph_galactic_primal(Seraph_Galactic x);
Seraph_Q128 seraph_galactic_tangent(Seraph_Galactic x);
bool seraph_galactic_is_void(Seraph_Galactic x);
```

## Implementation Notes

All Galactic operations are built on Q128 operations. The derivative rules are applied automatically:

```c
Seraph_Galactic seraph_galactic_mul(Seraph_Galactic a, Seraph_Galactic b) {
    // Product rule: (a*b)' = a'*b + a*b'
    Seraph_Q128 primal = seraph_q128_mul(a.primal, b.primal);
    Seraph_Q128 tangent = seraph_q128_add(
        seraph_q128_mul(a.tangent, b.primal),  // a' * b
        seraph_q128_mul(a.primal, b.tangent)   // a * b'
    );
    return (Seraph_Galactic){ primal, tangent };
}
```

## Forward vs Reverse Mode

SERAPH's Galactic numbers implement **forward mode** automatic differentiation:
- Compute derivative alongside value, left to right
- Best for functions f: R → R^n (one input, many outputs)
- Each "variable" tracks its own derivative

For f: R^n → R (many inputs, one output), reverse mode would be more efficient (like backpropagation). This could be a future extension.

## Relationship to Other Systems

| System | Similar To | Difference |
|--------|------------|------------|
| TensorFlow | tf.GradientTape | SERAPH uses fixed-point, no graphs |
| PyTorch | autograd | SERAPH is compile-time, not runtime |
| JAX | jax.grad | SERAPH integrates with memory safety |
| Dual numbers | C++ autodiff | SERAPH has VOID propagation |

## Source Files

| File | Description |
|------|-------------|
| `src/galactic.c` | Dual number arithmetic, automatic differentiation |
| `src/galactic_trig.c` | Galactic trigonometric functions with derivatives |
| `include/seraph/galactic.h` | Galactic structure, arithmetic rules, API |
