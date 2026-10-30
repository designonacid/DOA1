# DOA1 MCP server: turn an audio file into a DOA1 preset

This MCP server lets an LLM (Claude Desktop, Claude Code, or any MCP client) analyze an audio file,
search DOA1's parameters for a preset that sounds close, and refine it on request.

## How it works
1. **Analyze** the audio: pitch, attack and decay, how the brightness moves over time (filter
   envelope), harmonic structure (saw / square / triangle / sine / noise), unison movement, stereo width.
2. **Search**: render all factory presets plus three analysis-based starting points at the detected
   pitch, then refine the best with small parameter changes, keeping whatever measurably gets closer.
   Rendering uses `preset_lab`, which runs the real DOA1 engine, effects and arpeggiator without JUCE.
3. **Save** a `.doa1` preset (open it with the plugin's **LOAD** button) and a WAV preview.
4. **Refine**: the LLM uses `tweak_preset` ("brighter", "shorter", "less reverb") and `compare`,
   which reports whether each change got closer to the target.

## Tools
| tool | what it does |
|---|---|
| `analyze_audio(path)` | plain-language description of the sound |
| `match_audio(path, name, effort)` | finds a close preset; effort `quick` / `normal` / `thorough` |
| `render_preset(preset_path, note, seconds)` | WAV preview of a .doa1 preset |
| `tweak_preset(preset_path, changes, save_as, target_audio)` | edit parameters, optionally check against the target |
| `compare(preset_path, audio_path)` | feature-by-feature comparison and a distance |
| `describe_parameters()` | every parameter's range and meaning, plus sound-design tips |
| `list_factory_presets(category)` | the built-in presets |

## Setup
Use Python 3.10 or newer (the macOS system Python 3.9 cannot install the MCP SDK).
```
pip install "mcp>=1.2,<2" numpy scipy soundfile
cmake --build build --target preset_lab        # or let the server compile it on first use (needs a C++ compiler)
```

**Claude Desktop** (`claude_desktop_config.json`):
```json
{
  "mcpServers": {
    "doa1": {
      "command": "python3",
      "args": ["/path/to/DOA1/tools/doa1_mcp/server.py"],
      "env": { "DOA1_PRESET_LAB": "/path/to/DOA1/build/preset_lab" }
    }
  }
}
```

**Claude Code**: `claude mcp add doa1 -- python3 /path/to/DOA1/tools/doa1_mcp/server.py`

Presets and previews are written to `~/DOA1 Presets` (set `DOA1_OUTPUT_DIR` to change it).

## What to expect
- Best on an **isolated one-shot of one synth sound**: a single pluck, lead note, bass note, pad
  swell or stab, up to about 3 seconds. Export a single note from your track or sample.
- Full mixes, drums, vocals, chords and multi-note loops give a rough likeness at best.
- Measured on test sounds: pitch, waveform type, attack, unison and stereo width are matched well;
  decay times and how far the filter opens are matched less precisely. That is what `tweak_preset` is for.
- DOA1 is a subtractive synth: sounds built with FM, wavetables, samples or heavy processing can only
  be approximated.
