# Copilot Instructions

## Project Guidelines
- Always add log_write_boot() debug logging when implementing new features. The user explicitly requires this for every new feature.
- When debugging a crash or unknown issue, add as many log_write_boot() checkpoints as needed in a single pass — before and after every suspicious call, in every branch, in every thread entry point. Do not add minimal logging and iterate; instrument everything relevant at once to avoid multiple recompile/redeploy cycles.