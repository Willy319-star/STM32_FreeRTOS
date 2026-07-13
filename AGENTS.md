# Embedded Edge Learning Repository

## Goal

This repository is used to learn embedded development through observable
experiments, fault injection, measurement, and interview review.

## Working agreement

- Prefer learning and evidence over rapid code generation.
- Before editing, explain the current data path and intended change.
- Make the smallest independently testable change.
- Do not fabricate hardware results, timing, performance, or project status.
- Do not overwrite CubeMX-generated initialization or board configuration
  without explicitly identifying generated/user sections.
- Ask before changing pin assignments, clocks, power, boot configuration,
  flash partitions, or kernel configuration.
- Every milestone must include normal tests and at least three boundary/fault tests.
- Save learning records under `docs/learning/`.
- Save fault reports under `docs/faults/`.
- Save interview reports under `docs/interviews/`.
- Critical code must document execution context and data ownership.

## Code standards

- C11 for MCU and kernel-adjacent user code where practical.
- C++17 for Linux applications.
- Python 3.10+ for test tools.
- Check return values and error paths.
- Avoid hidden dynamic allocation in real-time paths.
- No busy loops without a documented reason.
- Use Git checkpoints before major Codex tasks.

## Testing

- Prefer host-side tests for parsers, CRC, serialization, and state machines.
- Hardware behavior must be validated on hardware.
- Measurements must include test conditions.
- Do not treat compilation success as sufficient validation.

## Codex learning behavior

Use the `embedded-learning-coach` skill for learning, building, debugging,
review, mock interviews, and resume evidence.
