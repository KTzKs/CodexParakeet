# CodexParakeet

CodexParakeet is a lightweight Windows application that converts Codex CLI responses into synthesized speech. It currently supports Japanese voice output using [VOICEVOX CORE](https://github.com/VOICEVOX/voicevox_core).

## Features and current limitations

- Japanese is currently the only supported language. Sorry for the limitation.
- The application uses spatial audio. On consoles that provide terminal/window position information, speech is played as if it came from the corresponding on-screen location.
- The available speech voices are **WhiteCUL**, **春日部つむぎ**, and **ずんだもん**.

## Runtime setup

The runtime folder is not committed to this repository. Create it with the setup script after placing VOICEVOX CORE under `third_party/voicevox_core/`:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\scripts\Initialize-Runtime.ps1 `
  -RuntimeRoot C:\Work\CodexParakeet
```

The script copies the Release executable, required DLLs, the notification script, VOICEVOX CORE files, and lip-sync images into the specified runtime directory.

After the script finishes, rename and move the generated runtime directory as appropriate for your environment. Keep the files together so that the following layout is preserved:

```text
CodexParakeet/
├─ CodexParakeet.exe
├─ codex_speak_notify.ps1
├─ lipsync/
└─ voicevox_core/
```

## Codex CLI configuration

Configure the Codex notification hook in `config.toml`. `notify` must be defined at the **top level** of the file. Do not put it below a section such as `[xxx]`; a section-scoped `notify` setting will not configure the top-level notification hook.

Set the path to the `codex_speak_notify.ps1` file in the runtime directory you installed:

```toml
notify = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "C:\\Work\\CodexParakeet\\codex_speak_notify.ps1"]
```

Replace the example path with the actual path on your machine.

## VOICEVOX CORE

CodexParakeet uses [VOICEVOX CORE](https://github.com/VOICEVOX/voicevox_core), an excellent Japanese speech synthesis engine. Obtain the appropriate release from the official repository and place its files under:

```text
third_party/voicevox_core/
```

Please review and comply with the licenses and usage terms of VOICEVOX CORE and its voice models.
