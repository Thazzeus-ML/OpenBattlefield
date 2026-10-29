# Security policy

## Scope

The simulation core is designed to run on authoritative game servers and to parse untrusted input:
terrain operations, snapshots and journals from the network or disk. Bugs in validation,
deserialization bounds checking, or arithmetic overflow are security issues.

Current protections: every deserializer is bounds checked and never throws; counts read from input
are capped before allocation; all operation parameters are validated against server limits;
arithmetic on coordinates is overflow checked; fuzz-style corruption tests run in CI
(`SnapshotDeserializationRejectsCorruptInput`).

## Reporting

Please report vulnerabilities privately through GitHub's "Report a vulnerability" (security
advisories) on this repository rather than a public issue. Include a reproduction (input bytes or a
test case) when possible. You will receive an acknowledgement within a week.

## Supported versions

Pre-1.0: only the `main` branch receives fixes.
