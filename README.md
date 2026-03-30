# Dice-throwing app for PS1

<img width="780" height="656" alt="dd17c459-db63-4716-b1c9-0c54e334f0a3" src="https://github.com/user-attachments/assets/0e3a35d6-5a0b-4155-a058-cef79631196a" />

A dice-throwing program written as a PlayStation 1 game ROM.

This project is based on [ps1_dev](https://github.com/marconvcm/ps1_dev) (MIT-licensed), which is in turn based on [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK).

## Build

Dependencies:
- Docker
- Ruby
- Make
- PS1 emulator (e.g., DuckStation)

To build the project, follow these steps:

```bash
# 1. Clone the repository:
git clone --recurse-submodules https://github.com/tslmy/ps1-dice.git
# 2. Build the Docker image:
make prepare
# 3. Build game:
make build
# 4. Run the game:
DUCKSTATION=/Applications/DuckStation.app/Contents/MacOS/DuckStation make emulate
# 5. Distribute the game:
make dist
```

It will create a zip file in the `dist` directory containing the game files.

## Plan

Here's our roadmap next up:

1. Add support for multiple dices. Consider their collisions.
2. Add support for different types of dices (D4, D6, D8, D10, D12, and D20).
3. Add a "dock"/"hotbar" at the bottom of the screen, like the one you see in Minecraft. Using the D-pad,
   - Use left & right buttons to select through different types of dices.
   - Use up & down buttons to dial up & down numbers of the dice type currently highlighted.
