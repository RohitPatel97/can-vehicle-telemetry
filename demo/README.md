# CAN telemetry demo

**[Open the live demo](https://rohitpatel97.github.io/can-vehicle-telemetry/)**
to share the walkthrough without downloading files. GitHub Pages regenerates
its capture from the published commit after the CI checks pass.

Open **[index.html](index.html)** in a browser. It works offline and includes
its captured execution data. No install is needed to view the ready-made demo.

The six scenarios replay actual compiled host execution: normal traffic, CRC
corruption, semantic range rejection, sequence recovery, timeout recovery, and
CAN-controller error accounting. The HAL section includes captured test results
and a separately labelled explanation of the backlog timeout fix.

- [Five-minute presenter guide](../docs/demo-walkthrough.md)
- [Machine-readable evidence](evidence.json)
- [Source project and validation limits](../README.md)

To refresh after rebuilding the project:

```sh
python scripts/create_demo.py --build-dir build
```

`template.html` is the rendering source; `index.html` is the generated replay.
The checked-in replay preserves the initial local capture; the live site has
fresh evidence from its deployment commit.
Capture metadata records the time, source revision, dirty-worktree flag, and
file hashes. Host execution and HAL doubles do not validate physical CAN/UART
behavior, interrupt latency, a complete Cube firmware build, or certification.
