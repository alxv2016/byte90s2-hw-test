# BYTE-90 Hardware Test Docs

Docs for the `byte90-demo` branch: a stripped-down firmware that exercises each
BYTE-90 peripheral in turn. Code is always the source of truth.

| If you need to… | Read |
| --- | --- |
| Flash the board, run the tests, read a failure | [`HARDWARE_TEST_GUIDE.md`](./HARDWARE_TEST_GUIDE.md) |
| Check pins, buses, I2C addresses, power rails | [`BYTE90_HARDWARE_SPECS.md`](./BYTE90_HARDWARE_SPECS.md) |
| Follow the house style | [`CODING_STYLE_GUIDE.md`](./CODING_STYLE_GUIDE.md) |

## Conventions

Each doc opens with the same three lines so an agent can route without reading
the body:

- **Scope** — what the document covers
- **Source of truth** — the code that wins if they disagree
- **Read this when** — the task that should bring you here

Prefer tables and file paths over prose. Do not add implementation history or
status checklists; those belong in commits and PRs.
