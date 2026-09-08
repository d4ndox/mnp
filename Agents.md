# MNP Codex Instructions

MNP is a C99 project for processing Monero wallet-rpc notifications
using named pipes.

## General rules

- Keep changes small and focused.
- Explain planned changes before making large modifications.
- Do not commit unless explicitly requested.
- Never push to GitHub unless explicitly requested.
- Never force-push.
- Do not modify unrelated files.
- Preserve existing command-line compatibility unless explicitly requested.

## Build and tests

- Use the existing CMake build system.
- Run relevant tests after modifications.
- Prefer changes under tests/ when working on test tasks.
- Report failing tests instead of hiding or weakening them.
- Do not change production code merely to make a bad test pass.

## Git

- Work only on the currently checked-out branch.
- Do not switch branches unless explicitly requested.
- Do not create commits unless explicitly requested.
- Never use git reset --hard without explicit permission.
- Never discard existing user changes.

## Monero / security

- Never expose wallet passwords.
- Never print or commit RPC credentials.
- Never commit wallet files or .keys files.
- Never initiate real Mainnet transfers.
- Never modify real wallet data.
- Use stagenet/test infrastructure for transaction tests.
- Treat environment variables containing passwords or RPC credentials as secrets.

## Shell

- Prefer portable shell where practical.
- Quote variables correctly.
- Avoid destructive commands.
