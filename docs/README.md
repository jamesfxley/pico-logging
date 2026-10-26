# pico-logging documentation

| Document | What it is | Read it when |
|---|---|---|
| [traceability-exceptions.txt](traceability-exceptions.txt) | Requirements no source file can cite (EN-02.7). | Reviewing uncitable requirements. |
| [orientation.md](orientation.md) | How this repository is built and gated, how it is laid out, and the repository's invariants and traps | Working in it for the first time, or before an edit |
| [technical-specification.md](technical-specification.md) | **Normative.** Requirement IDs (`AR`/`FR`/`BR`/`CR`/`OR`), their pass criteria, and the traceability matrix | Deciding whether a behaviour is required or incidental, or changing one |
| [architecture.md](architecture.md) | Descriptive. How the implementation is put together — a statement's life, the two filters, the float formatter, the sinks | Working out where a change belongs |
| [usage-guide.md](usage-guide.md) | Practical use beyond the README: bringing it up in firmware, choosing a filter, the per-TU ceiling, the no-allocation guarantee, logging from an ISR | Building an application on the library |
| [deviations.md](deviations.md) | Standing (scope-wide) deviations from the coding standard, each with a rationale, a risk assessment and a review date | Adding or reviewing a suppression |
| [deviation-budget.txt](deviation-budget.txt) | Per-language suppression ceilings. These only ever ratchet down | Same |
| [requirement-prefixes.txt](requirement-prefixes.txt) | The requirement prefix convention checked by review | Adding a requirement under a new prefix |
| [GitHub Releases](https://github.com/jamesfxley/pico-logging/releases) | What changed in each release, and what to change in your code when an API broke | Upgrading across a release |

Measured results are not in this directory. A run record lives beside the rig
that produced it, with the conditions it was taken under (`EN-03.2`):
[tests/device/measurements.md](../tests/device/measurements.md) for the AR-2
device run, [tests/firmware/measurements.md](../tests/firmware/measurements.md)
for the cross-compiled probe's footprint.

The specification is the only normative document here. `architecture.md`
describes what is, and work deliberately not done is an issue in the tracker
rather than a document in the tree (`EN-03.7`).

A behaviour claimed in `README.md` or `docs/orientation.md` but absent from the
specification is not a commitment, and a requirement is not satisfied until the
matrix names what establishes it.

The repository `README.md` is the surface — the macros, the filters, the `Sink`
interface. `usage-guide.md` is the other half: bringing it up in a real image.

The API reference is generated from the headers rather than written by hand, so
it cannot drift from the declarations it documents:

```sh
doxygen docs/Doxyfile      # -> docs/api/html/index.html (gitignored)
```

The `api-docs` gate builds it on every run and fails on an undocumented public
entity.

## Conventions

- Requirement IDs are stable and never renumbered. Code and tests cite the ID,
  never a section number: IDs survive a reorganisation of this document and
  section numbers do not. Review checks that specification IDs are cited by code or tests;
  `requirement-prefixes.txt` records their prefix convention.
- The specification states what is true now — no dates, no version history, no
  measured values. It may state a normative gate ("within a CI-enforced
  budget") but not a measurement; a measured figure is a run record and lives
  beside the rig that produced it (`EN-03.2`).
- A behaviour change lands as one change: the specification row, a host test,
  and the affected documents together.
