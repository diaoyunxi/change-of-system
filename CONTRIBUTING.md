# Contributing

## How to Contribute

### Reporting Bugs

1. Check existing issues first
2. Include: OS, compiler version, steps to reproduce, crash logs if available

### Submitting Pull Requests

1. Fork and branch from `main`
2. Follow the existing C++ coding style
3. Ensure code compiles without warnings
4. Add tests for new functionality
5. Submit a PR with clear description

### Code Style

- Use modern C++ (C++17 or later)
- Prefer RAII and smart pointers over raw pointers
- Use `std::string` over C-style strings
- Use `std::vector` over raw arrays
- Prefer `std::unique_ptr` over `new`/`delete`
- Avoid global mutable state

### Commit Format

```
type(module): description
```

Types: `fix`, `feat`, `refactor`, `perf`, `docs`, `chore`
