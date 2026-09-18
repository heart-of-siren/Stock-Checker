# Stock Checker

Stock Checker is an SKSE plugin for **The Elder Scrolls V: Skyrim Special Edition / Anniversary Edition** that adds a searchable merchant-stock browser to **Skyrim Menu Framework**.

Search for an item and Stock Checker shows the merchants who currently have it, their current location, and the quantity in stock.

## Features

- Case-insensitive partial-name searching.
- Auto-populated item dropdown sourced from live merchant stock.
- Press **Enter** to submit a text search and display all matching stocked items.
- Results are displayed as **Vendor | Location | Qty**.
- Searches the current state of the save rather than a hardcoded merchant or item list.
- Supports compatible mod-added items and merchants automatically.
- Excludes unresolved merchant services, dead merchants, and actors currently in Skyrim's follower faction.
- Khajiit caravan merchants show their current named stop when available, or **Travelling** while between stops.
- Full stock refresh after loading/starting a game and whenever the Barter Menu closes.
- No continuous global rescanning every frame.

## Usage

Open Skyrim Menu Framework and choose:

**Stock Checker → Stock Checker - Search**

Start typing to filter the item dropdown. Select an item from the dropdown to display that item immediately.

You can also type a search and press **Enter**. For example, a search such as `Daedric` displays all currently stocked items whose names contain that text, each with its current vendor results.

Selecting an item from the dropdown clears the text search and switches back to a single-item result.

## How stock is determined

Stock Checker discovers merchant services from the game and reads their current merchant containers. A merchant service only contributes to the searchable stock index when it can be resolved to a current Actor.

Stock Checker then screens the resolved actor's current state. Dead merchants and actors currently in `CurrentFollowerFaction` are excluded.

The searchable item list therefore contains only items currently present in at least one resolved, eligible merchant's stock source.

## Refresh behaviour

A full stock dive runs:

- after loading a save;
- when starting a new game;
- whenever the Barter Menu closes.

The refresh rebuilds current actor/faction associations, merchant eligibility, locations, and live inventory data.

## Requirements

- Skyrim Special Edition / Anniversary Edition
- SKSE64
- Address Library for SKSE Plugins
- Skyrim Menu Framework

Tested on Skyrim runtime **1.6.1170**.

## Compatibility

Stock Checker does not edit vanilla records.

Compatible mods that use Skyrim's normal merchant faction/container mechanisms can be discovered automatically. Highly custom scripted merchant systems may not be discoverable.

## Building

See [BUILDING.md](BUILDING.md).

## Source

This repository contains the public source for Stock Checker.

## Credits

- SKSE Team
- CommonLibSSE-NG contributors
- Skyrim Menu Framework contributors
- Address Library contributors
