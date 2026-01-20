# AGENTS.md

## Purpose

This file defines the rules, expectations, and constraints for **automated agents**
(including AI coding assistants, LLMs, bots, and scripted tools) contributing to this
codebase.

The goal is to:
- Preserve long-term maintainability
- Avoid licensing and provenance risk
- Prevent architectural drift
- Ensure consistency with established design decisions
- Make human review straightforward and reliable

All automated agents MUST comply with the requirements in this document.

---

## Scope

This repository is a **C-based MUD engine** derived from CircleMUD and is
under active development, including significant refactors to core systems such as:

- Skills and proficiency
- Combat resolution
- Object and mobile data models
- Online Creation (OLC)
- Persistence and serialization
- Builder and immortal tooling

Changes often have **far-reaching consequences** across gameplay, balance, and
world data integrity.

---

## General Rules for Automated Agents

### 1. No Unreviewed Structural Changes

Automated agents MUST NOT:
- Reorganize directories
- Rename files
- Merge or split source files
- Introduce new subsystems
- Replace existing systems wholesale

Unless **explicitly instructed** to do so.

Incremental, targeted changes are required.

---

### 2. Preserve Existing Behavior Unless Directed

If a function, macro, or subsystem already exists:
- Do NOT change semantics
- Do NOT “simplify” logic
- Do NOT remove edge-case handling
- Do NOT refactor stylistically

Unless the user **explicitly requests** that behavior be changed.

Backward compatibility is a priority.

---

### 3. Minimal Diffs Are Mandatory

Automated agents must:
- Change only what is necessary
- Avoid drive-by formatting edits
- Avoid re-indentation unless required
- Avoid renaming variables unless necessary for correctness

If a fix can be achieved with a 3-line change, a 30-line rewrite is unacceptable.

---

### 4. Follow Existing Code Style Exactly

This codebase intentionally reflects legacy CircleMUD conventions.

Agents MUST:
- Match indentation style
- Match brace placement
- Match naming conventions
- Match macro usage patterns

Do not introduce modern C idioms, new abstractions, or stylistic preferences.

---

## Licensing and Provenance Requirements

### 5. No Third-Party Code Injection

Automated agents MUST NOT:
- Paste code from external projects
- Introduce snippets from blogs, gists, StackOverflow, or forums
- Reproduce code from GPL-incompatible sources

All code must be **original**, **derivative of existing repository code**, or
**explicitly authorized** by the user.

If uncertain, ask before proceeding.

---

### 6. Do Not Assume License Changes

Do NOT:
- Modify license headers
- Remove attribution
- Add new license files
- Assume relicensing is permitted

Licensing is handled deliberately and conservatively.

---

## Technical Expectations

### 7. Full-Context Awareness Required

Before modifying a subsystem, automated agents MUST:
- Read all related `.c` and `.h` files
- Identify existing macros, helpers, and patterns
- Understand how data flows through the system

Guessing or partial understanding is not acceptable.

---

### 8. Prefer Existing Helpers and Macros

If functionality already exists:
- Reuse it
- Extend it minimally if needed
- Do not reimplement logic elsewhere

Duplication increases maintenance cost and risk.

---

### 9. Explicitly Note Assumptions

When producing code or recommendations, agents MUST:
- State assumptions clearly
- Identify uncertainties
- Call out areas that require human confirmation

Silent assumptions are dangerous.

---

## Data Integrity and World Safety

### 10. Protect World Files and Player Data

Automated agents must treat:
- World files
- Player files
- OLC data
- Serialized objects/mobs/rooms

As **production data**.

Do NOT:
- Change file formats casually
- Break backward compatibility
- Introduce implicit migrations

Any data format change must be explicit and documented.

---

## Communication Expectations

### 11. Be Direct and Precise

Agent output should:
- Use technical language appropriate to experienced developers
- Avoid verbosity for its own sake
- Avoid motivational or conversational filler
- Focus on correctness and clarity

---

### 12. Ask Before Acting When Uncertain

If instructions are ambiguous or risky:
- STOP
- Ask clarifying questions
- Do not guess intent

Incorrect confidence is worse than delay.

---

## Enforcement

Failure to comply with this document may result in:
- Rejection of changes
- Reversion of commits
- Loss of permission to contribute

This applies equally to humans and automated agents.

---

## Summary

This codebase prioritizes:
- Stability over novelty
- Clarity over cleverness
- Intentional design over convenience

Automated agents are welcome collaborators **only** when they operate within
these constraints.
