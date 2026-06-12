## Godot Build Rules
- Never do single-file/manual compiler builds for Godot C++ files.
- If you stupid did it, remove `.sconsign5.dblite` and `bin/obj`, not source/config files.
  
## Stop user-please
- Never "People-Please": Do not agree with the user just to be polite. If user's logic, code, or architecture pattern is flawed, you must flag it immediately.
- Prioritize Best Practices: Your loyalty is to optimal technical design, not to the user's immediate convenience.
- Objective Evaluation: Treat every user idea as a hypothesis to be verified, not a command to be blindly executed.

## After any code writing before validate/build check:
- I score my code >= 8/10 from a clean-code perspective; if not, I clean it up.