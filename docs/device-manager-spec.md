# Network Device Manager — Full-Stack Specification

**Version:** 1.0
**Status:** Superseded as a design. Kept as the source of the device domain (power, flashing, probe plugin categories; desired vs. actual state).

> **Read `CONTEXT.md` and `docs/adr/` first.** qwe is not this service. It's a CLI that runs workflows to completion (see `workflow-kernel-design.md`). Terms that changed meaning:
> - "Controller node" here means the machine running the whole service. In qwe, the **controller** is a remote target wired to devices, with no qwe on it (ADR-0002).
> - "Job" here means a device operation. In qwe that's a **workflow run**, and a **job** is a node in the workflow graph.
> - Devices are never targets.
> - VLAN automation is out of scope (switch VLANs are configured by hand).
> - The REST API, dashboard and serial console belong to a future `qwe serve`, if anywhere.
> - There is no Python in qwe.
**Scope:** A single controller node running a FastAPI service, a thin htmx dashboard, and per-device serial consoles, managing a small fleet (4–10) of network devices whose power, OS image, flashing method, and VLAN placement are controlled through typed plugins.

---

## 1. Overview

The Device Manager runs on one **controller node** physically wired to 4–10 **managed devices**. For each device the controller can:

- Query live state (power state, running OS).
- List boot images available in Artifactory and set a desired image.
- Drive state transitions: power on/off, and **flash** (assign image → power off → write image via the device's flashing method → power on).
- Open an interactive **serial console** over USB-to-serial, in the browser, via xterm.js.

The system is deliberately minimal in layers: FastAPI + Pydantic for the API and validation, Jinja2 + htmx for the UI (no React, no build step), and a plugin system that isolates the many ways a device can be powered, flashed, and networked.

### Design principles

1. **Desired vs. actual state.** Configuration expresses *what should be true*; live probes report *what is true*; operations reconcile the two.
2. **Typed everywhere.** Every plugin owns a Pydantic config model. No opaque `Dict[str, Any]` on the wire — this keeps both validation and the auto-generated Swagger UI honest.
3. **Stable public contract, swappable internals.** Endpoints like `POST /devices/{id}/flash` are the public API; which plugin implements the flash (netboot / fastboot / sd-mux) is an internal dispatch the caller never sees.
4. **Long operations are jobs.** Anything that takes more than a second or two (flashing especially) runs as an async job with a queryable progress record.
5. **Two UIs from one API.** Stock Swagger UI is the operation console for typed, one-off calls; the thin dashboard is the fleet view. Both are clients of the same JSON/WebSocket API.

---

## 2. Physical Topology

```
                          ┌───────────────────────────────────────┐
                          │            Controller Node             │
                          │                                         │
   Operator ── HTTP/WS ──▶│  FastAPI  ─ REST + WebSocket            │
   (browser)              │  Dashboard (Jinja2 + htmx)              │
                          │  Serial bridges (pyserial-asyncio)      │
                          │  Job manager (in-memory)                │
                          └───┬───────────┬───────────┬────────────┘
                              │ USB-serial │ network   │ power ctrl
             ┌────────────────┼────────────┼───────────┼───────────┐
             ▼                ▼            ▼           ▼            ▼
        ┌─────────┐     ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐
        │ Device1 │     │ Device2 │  │ Device3 │  │  ...    │  │ DeviceN │
        │ ttyUSB  │     │ ttyUSB  │  │ ttyUSB  │  │         │  │ ttyUSB  │
        │ PoE     │     │ socket  │  │ sd-mux  │  │         │  │ serial  │
        └─────────┘     └─────────┘  └─────────┘  └─────────┘  └─────────┘
```

The controller reaches each device through up to four independent channels, any subset of which may be present:

| Channel | Purpose | Backed by |
|---|---|---|
| USB→serial | Interactive console + optional OS probing | `/dev/serial/by-id/...` (udev-stable) |
| Network (LAN/VLAN) | Reachability, OS agent, netboot | Ethernet + managed switch |
| Power control | On/off/cycle | PoE port, networked socket, or serial relay |
| Flashing transport | Write OS image | netboot server, fastboot USB, or sd-mux |

Because a device's power/flash/network capabilities differ, each is expressed as a plugin selection in that device's config.

---

## 3. Technology Stack

| Layer | Choice | Notes |
|---|---|---|
| Language | Python 3.12+ | Modern typing, `Literal`, `Annotated` unions |
| API framework | FastAPI | Auto OpenAPI/Swagger, native WebSockets |
| Validation/models | Pydantic v2 | Discriminated unions for plugin configs |
| ASGI server | Uvicorn | `uvicorn app.main:app` |
| Templating | Jinja2 | Server-rendered HTML fragments |
| Frontend interactivity | htmx | Polling + SSE, no build step, no React |
| Terminal UI | xterm.js | Serial console in the browser |
| Server-sent events | sse-starlette | Job progress streaming to htmx |
| Serial I/O | pyserial-asyncio | Non-blocking read/write to `/dev/tty*` |
| Config | PyYAML | Static device + plugin catalog |
| HTTP client | httpx | Artifactory calls; also the recommended client lib for Python consumers |
| Packaging | pyproject.toml (uv or pip) | Single deployable app |

No database is required at this scale. Jobs and serial sessions are in-memory (see §10.4 and §11.4). SQLite is an optional upgrade if job history must survive restarts.

---

## 4. Core Concepts

### 4.1 Desired vs. actual state

- **Actual** (queried live, never stored authoritatively):
  - `power_state` — on / off / unknown, from the power plugin.
  - `running_os` — identifier of the currently booted image, from a **probe** plugin (or `unknown` if the device is off / unprobeable).
- **Desired** (stored in config / runtime overrides):
  - `desired_image` — the image the device *should* run.

### 4.2 Reconciliation

`flash` is the reconcile operation that makes actual converge to desired:

```
assign desired_image → power off → write image (flashing plugin) → power on → (optional) verify running_os == desired_image
```

Each step is reported as job progress. Power on/off are also exposed as standalone primitives for manual control and debugging.

### 4.3 Plugins

Four plugin categories, each an abstract base with a typed config model:

| Category | Responsibility | Example implementations |
|---|---|---|
| **Power** | on / off / cycle / status | `poe`, `network_socket`, `serial_relay` |
| **Flashing** | write an image to the device | `netboot`, `fastboot`, `sd_mux` |
| **VLAN** | place device ports on VLANs | `switch_api`, `vxlan` |
| **Probe** *(optional)* | report `running_os` and health | `ssh_agent`, `serial_banner`, `http_metadata` |

A device references one plugin per category by `type`; the plugin's config travels as a **discriminated union** variant, so it is fully typed and Swagger-rendered.

---

## 5. Data Models (Pydantic v2)

### 5.1 Plugin config unions

Each plugin variant carries a `Literal` `type` field acting as the discriminator. Swagger renders these as a type dropdown with the correct fields per variant.

```python
from typing import Literal, Union, Annotated, Optional
from pydantic import BaseModel, Field

# ---- Power ----
class PoEPowerConfig(BaseModel):
    type: Literal["poe"] = "poe"
    switch_ip: str
    port_number: int

class NetworkSocketPowerConfig(BaseModel):
    type: Literal["network_socket"] = "network_socket"
    socket_ip: str
    outlet_number: int

class SerialRelayPowerConfig(BaseModel):
    type: Literal["serial_relay"] = "serial_relay"
    serial_port: str
    channel: int

PowerConfig = Annotated[
    Union[PoEPowerConfig, NetworkSocketPowerConfig, SerialRelayPowerConfig],
    Field(discriminator="type"),
]

# ---- Flashing ----
class NetbootFlashConfig(BaseModel):
    type: Literal["netboot"] = "netboot"
    tftp_root: str
    mac_address: str            # target boots into installer via PXE/iPXE

class FastbootFlashConfig(BaseModel):
    type: Literal["fastboot"] = "fastboot"
    usb_serial: str             # fastboot device id
    partition_map: dict[str, str]  # partition -> image artifact name

class SdMuxFlashConfig(BaseModel):
    type: Literal["sd_mux"] = "sd_mux"
    sdmux_id: str               # usbsdmux device id
    block_device: str           # host path when SD is switched to host

FlashingConfig = Annotated[
    Union[NetbootFlashConfig, FastbootFlashConfig, SdMuxFlashConfig],
    Field(discriminator="type"),
]

# ---- VLAN ----
class SwitchApiVlanConfig(BaseModel):
    type: Literal["switch_api"] = "switch_api"
    switch_ip: str
    port: str

class VxlanVlanConfig(BaseModel):
    type: Literal["vxlan"] = "vxlan"
    controller_ip: str

VlanConfig = Annotated[
    Union[SwitchApiVlanConfig, VxlanVlanConfig],
    Field(discriminator="type"),
]

# ---- Probe (optional) ----
class SshProbeConfig(BaseModel):
    type: Literal["ssh_agent"] = "ssh_agent"
    host: str
    username: str

class SerialBannerProbeConfig(BaseModel):
    type: Literal["serial_banner"] = "serial_banner"
    match_patterns: dict[str, str]  # regex -> os identifier

ProbeConfig = Annotated[
    Union[SshProbeConfig, SerialBannerProbeConfig],
    Field(discriminator="type"),
]
```

### 5.2 Device and console

```python
class SerialConfig(BaseModel):
    device_path: str            # udev-stable, e.g. /dev/serial/by-id/usb-FTDI_...
    baudrate: int = 115200

class Device(BaseModel):
    id: str
    hostname: str
    mac_address: str
    family: str                 # used to filter compatible images (arch/board)
    location: Optional[str] = None
    serial: Optional[SerialConfig] = None
    power: PowerConfig
    flashing: FlashingConfig
    vlan: Optional[VlanConfig] = None
    probe: Optional[ProbeConfig] = None
    desired_image: Optional[str] = None   # may be overridden at runtime
```

### 5.3 Live status and capabilities

```python
class DeviceStatus(BaseModel):
    id: str
    power_state: Literal["on", "off", "unknown"]
    running_os: Optional[str]          # None if off/unprobeable
    desired_image: Optional[str]
    reachable: bool
    active_job: Optional[str]          # job id if an operation is in flight

class DeviceCapabilities(BaseModel):
    """What operations are valid for this device given its configured plugins.
    Consumed by the custom dashboard to show only relevant controls."""
    id: str
    can_power: bool
    can_flash: bool
    can_console: bool
    can_vlan: bool
    flash_method: str                  # e.g. "sd_mux"
    power_method: str                  # e.g. "poe"
```

### 5.4 Images

```python
class BootImage(BaseModel):
    name: str                          # stable id, referenced by desired_image
    family: str                        # must match Device.family to be offered
    version: str
    artifactory_path: str
    size_bytes: Optional[int] = None
    checksum: Optional[str] = None
```

### 5.5 Jobs

```python
class JobStep(BaseModel):
    name: str                          # "assign", "power_off", "write", "power_on", "verify"
    state: Literal["pending", "running", "done", "failed", "skipped"]
    message: Optional[str] = None
    started_at: Optional[str] = None
    finished_at: Optional[str] = None

class Job(BaseModel):
    id: str
    device_id: str
    kind: Literal["flash", "power_cycle"]
    state: Literal["queued", "running", "succeeded", "failed"]
    steps: list[JobStep]
    created_at: str
    updated_at: str
    error: Optional[str] = None
```

---

## 6. Plugin System

### 6.1 Base interfaces

```python
from abc import ABC, abstractmethod

class PowerPlugin(ABC):
    def __init__(self, config): self.config = config
    @abstractmethod
    async def power_on(self, device: Device) -> None: ...
    @abstractmethod
    async def power_off(self, device: Device) -> None: ...
    @abstractmethod
    async def status(self, device: Device) -> str: ...   # "on"|"off"|"unknown"

class FlashingPlugin(ABC):
    def __init__(self, config): self.config = config
    @abstractmethod
    async def write_image(self, device: Device, image: BootImage,
                          progress) -> None: ...          # progress(pct, msg)

class VlanPlugin(ABC):
    def __init__(self, config): self.config = config
    @abstractmethod
    async def apply(self, device: Device, vlan_id: int) -> None: ...

class ProbePlugin(ABC):
    def __init__(self, config): self.config = config
    @abstractmethod
    async def running_os(self, device: Device) -> Optional[str]: ...
```

### 6.2 Registry

The registry maps a `type` string to a `(config_model, plugin_class)` pair per category. It is populated at import time (static set) — the simplest, and recommended, approach for a known-at-deploy plugin set.

```python
class PluginRegistry:
    def __init__(self):
        self._power, self._flash, self._vlan, self._probe = {}, {}, {}, {}

    def register_power(self, key, model, cls): self._power[key] = (model, cls)
    def build_power(self, cfg: PowerConfig) -> PowerPlugin:
        _, cls = self._power[cfg.type]
        return cls(cfg)
    # ...analogous for flash / vlan / probe

registry = PluginRegistry()
registry.register_power("poe", PoEPowerConfig, PoEPowerPlugin)
registry.register_power("network_socket", NetworkSocketPowerConfig, NetworkSocketPowerPlugin)
registry.register_power("serial_relay", SerialRelayPowerConfig, SerialRelayPowerPlugin)
registry.register_flash("netboot", NetbootFlashConfig, NetbootFlashPlugin)
registry.register_flash("fastboot", FastbootFlashConfig, FastbootFlashPlugin)
registry.register_flash("sd_mux", SdMuxFlashConfig, SdMuxFlashPlugin)
# ...vlan, probe
```

Because the config unions are discriminated, Pydantic already validated and parsed each device's plugin config at load time; the registry only needs to instantiate the matching class.

### 6.3 Adding a plugin

1. Add a `*Config` model with a new `Literal` `type` and its fields.
2. Add the variant to the relevant `Union`.
3. Implement the `*Plugin` class against the base interface.
4. `registry.register_*(...)` it.
5. Reference it in a device's config.

No API or dashboard code changes — endpoints dispatch through the registry, and Swagger picks up the new config variant automatically.

### 6.4 Dynamic discovery (optional, deferred)

If plugins must be discovered fully at runtime (e.g. dropped into a `plugins/` dir), build the discriminated `Union` from the registered variants at startup instead of defining it statically. This is extra machinery and unnecessary for the initial known set; noted here so the static choice is a deliberate one.

---

## 7. Configuration

### 7.1 `config/devices.yaml`

```yaml
devices:
  dev-01:
    hostname: edge-router-01
    mac_address: "aa:bb:cc:dd:ee:01"
    family: arm64-rpi4
    location: "Rack A / Slot 1"
    desired_image: rpi4-debian-12
    serial:
      device_path: /dev/serial/by-id/usb-FTDI_FT232R_A50285BI-if00-port0
      baudrate: 115200
    power:
      type: poe
      switch_ip: 192.168.10.2
      port_number: 5
    flashing:
      type: sd_mux
      sdmux_id: sdmux-0001
      block_device: /dev/sda
    vlan:
      type: switch_api
      switch_ip: 192.168.10.2
      port: "Gi1/0/5"
    probe:
      type: ssh_agent
      host: 192.168.20.11
      username: admin

  dev-02:
    hostname: test-board-02
    mac_address: "aa:bb:cc:dd:ee:02"
    family: x86_64-uefi
    location: "Rack A / Slot 2"
    serial:
      device_path: /dev/serial/by-id/usb-FTDI_FT232R_A50285CD-if00-port0
    power:
      type: network_socket
      socket_ip: 192.168.10.50
      outlet_number: 3
    flashing:
      type: netboot
      tftp_root: /srv/tftp
      mac_address: "aa:bb:cc:dd:ee:02"
```

### 7.2 Image catalog

Images live in Artifactory. The catalog can be a static YAML mirror or fetched live via httpx; either way it is filtered by `Device.family` before being offered.

```yaml
images:
  - name: rpi4-debian-12
    family: arm64-rpi4
    version: "12.4"
    artifactory_path: "artifactory/os/rpi4/debian-12.4.img.xz"
    checksum: "sha256:..."
  - name: x86-ubuntu-2404
    family: x86_64-uefi
    version: "24.04"
    artifactory_path: "artifactory/os/x86/ubuntu-24.04.iso"
```

### 7.3 Stable serial naming (udev)

`ttyUSBn` numbering is **not** stable across reboots/replug. Devices must be addressed by `/dev/serial/by-id/...` or a custom udev symlink. Ship a rules file:

```
# udev/99-device-manager.rules
SUBSYSTEM=="tty", ATTRS{serial}=="A50285BI", SYMLINK+="devmgr/dev-01"
SUBSYSTEM=="tty", ATTRS{serial}=="A50285CD", SYMLINK+="devmgr/dev-02"
```

Then `device_path: /dev/devmgr/dev-01`. This binds a physical adapter to a logical device regardless of enumeration order.

---

## 8. REST API

All request/response bodies are Pydantic models, so the Swagger UI at `/docs` is fully typed and try-it-out works out of the box.

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/devices` | List devices with summary status |
| `GET` | `/devices/{id}` | Device config detail |
| `GET` | `/devices/{id}/status` | **Live** status: power, running OS, reachable, active job |
| `GET` | `/devices/{id}/capabilities` | Which operations are valid (drives dashboard controls) |
| `GET` | `/devices/{id}/images` | Compatible boot images (filtered by `family`) |
| `PUT` | `/devices/{id}/desired-image` | Set desired image (`{ "name": "..." }`) |
| `POST` | `/devices/{id}/power/on` | Primitive: power on |
| `POST` | `/devices/{id}/power/off` | Primitive: power off |
| `POST` | `/devices/{id}/power/cycle` | Primitive: off→on (returns a job) |
| `POST` | `/devices/{id}/flash` | Reconcile to desired image (returns a job) |
| `GET` | `/jobs` | List jobs (optionally `?device_id=`) |
| `GET` | `/jobs/{job_id}` | Job detail + steps |
| `GET` | `/jobs/{job_id}/events` | **SSE** stream of job progress |
| `WS` | `/devices/{id}/console` | Serial console WebSocket (see §11) |

### 8.1 Semantics

- **Primitives vs. composites.** `power/on|off` return synchronously (fast). `flash` and `power/cycle` are multi-step and **return a job** (`202 Accepted`, body is a `Job` with `id`). Callers poll `/jobs/{id}` or subscribe to `/jobs/{id}/events`.
- **`flash` body** is optional: `{ "image": "<name>" }` sets desired image inline before reconciling; omitted, it uses the stored `desired_image`. `409 Conflict` if none is set.
- **Concurrency guard.** A device already running a job rejects a second mutating operation with `409` and the active job id (also surfaced in `DeviceStatus.active_job`).
- **Errors** use standard HTTP codes with a typed error body; plugin failures become `500` with the plugin's message, and are also recorded on the failing job step.

---

## 9. Flash Workflow (reconcile)

`POST /devices/{id}/flash` creates a `Job(kind="flash")` with these steps, executed by the reconciler:

| Step | Action | Plugin |
|---|---|---|
| `assign` | Record desired image, resolve image metadata from catalog | — |
| `power_off` | Ensure device is off before writing | PowerPlugin |
| `write` | Write the image; emits granular `progress(pct, msg)` | FlashingPlugin |
| `power_on` | Boot into the new image | PowerPlugin |
| `verify` *(optional)* | Probe `running_os`, assert it matches desired | ProbePlugin |

The `write` step differs entirely by plugin:

- **netboot** — stage image in `tftp_root`, set the device to PXE/iPXE boot, power on into the installer, wait for completion signal.
- **fastboot** — enter fastboot, `flash` each partition per `partition_map`, reboot.
- **sd_mux** — switch the SD card to the host, write the block device, switch back to the device.

The reconciler treats all three uniformly through `FlashingPlugin.write_image(...)`. Progress from the plugin is streamed onto the job's `write` step and out through SSE to the dashboard.

---

## 10. Async Job System

### 10.1 Lifecycle

`queued → running → succeeded | failed`. Each step carries its own state, message, and timestamps, so the dashboard can render a live checklist.

### 10.2 Execution

Jobs run as asyncio tasks owned by a `JobManager`. The manager exposes `create(kind, device_id, coro_factory)`, tracks the task, and updates the `Job` record as steps progress. A per-device lock prevents overlapping mutating jobs.

### 10.3 Progress delivery

- **SSE (preferred):** `GET /jobs/{id}/events` yields a JSON event on every step/percent change via `sse-starlette`. The dashboard consumes it with the htmx SSE extension — no polling, near-instant updates.
- **Polling (fallback):** `GET /jobs/{id}` returns the full record; htmx can poll a fragment on an interval where SSE is undesirable.

### 10.4 Storage

In-memory dict keyed by job id, bounded with a retention cap (e.g. last N per device). Acceptable because a single controller with ≤10 devices runs few concurrent jobs and history need not survive restarts. **Optional upgrade:** persist to SQLite for durable history/audit.

---

## 11. Serial Console Subsystem

The most hardware-coupled part. Goal: an operator opens a device page, clicks *Console*, and gets a live xterm.js terminal on the device's USB serial line.

### 11.1 Data path

```
xterm.js (browser)  ⇄  WebSocket  ⇄  SerialSession (asyncio)  ⇄  pyserial-asyncio  ⇄  /dev/devmgr/dev-0x
```

### 11.2 SerialSession (per device, shared)

Only one OS process may hold a serial port open, so the controller owns a single `SerialSession` per device, opened lazily on first viewer:

- One reader coroutine pumps **serial → all attached WebSockets** (broadcast).
- Input from any attached socket is written **WebSocket → serial**.
- Sessions are reference-counted; the port is released when the last viewer detaches (or kept warm with a configurable idle timeout to preserve scrollback continuity).

Concurrency policy: multiple viewers may attach; either allow all to type (simple) or designate the first as read-write and others read-only (safer for shared debugging). Read-write-for-all is the default; the policy is a session flag.

### 11.3 WebSocket endpoint

```python
@router.websocket("/devices/{device_id}/console")
async def console(ws: WebSocket, device_id: str):
    device = devices[device_id]
    if not device.serial:
        await ws.close(code=4404); return
    session = serial_manager.get_or_open(device)   # shared, lazy
    await ws.accept()
    await session.attach(ws)
    try:
        async for data in ws.iter_bytes():
            await session.write(data)              # keystrokes -> serial
    except WebSocketDisconnect:
        pass
    finally:
        await session.detach(ws)                   # releases port if last
```

Protocol is raw bytes both directions (xterm.js emits keystrokes, renders received bytes). A small control channel (JSON text frames) can carry resize (`{"type":"resize","cols":..,"rows":..}`) and break signals if needed.

### 11.4 Browser side

```html
<div id="term"></div>
<script src="/static/xterm.js"></script>
<link rel="stylesheet" href="/static/xterm.css">
<script>
  const term = new Terminal({ convertEol: true, scrollback: 5000 });
  term.open(document.getElementById('term'));
  const ws = new WebSocket(`ws://${location.host}/devices/{{ device.id }}/console`);
  ws.binaryType = 'arraybuffer';
  ws.onmessage = e => term.write(new Uint8Array(e.data));
  term.onData(d => ws.send(new TextEncoder().encode(d)));
  // reconnect-with-backoff wrapper omitted for brevity
</script>
```

A thin reconnect wrapper (exponential backoff) keeps long-lived consoles alive across network blips and controller restarts.

### 11.5 Probe reuse

A `serial_banner` probe can non-intrusively tap the same session's output stream to infer `running_os` from boot banners, avoiding a second port open.

---

## 12. Dashboard

Server-rendered Jinja2 pages enhanced with htmx. No React, no bundler, no client-side routing. htmx swaps HTML fragments returned by the API/dashboard routes; SSE drives live job/status updates.

### 12.1 Pages

| Route | Page | Contents |
|---|---|---|
| `GET /` | Fleet view | Table of all devices: power state, running OS, desired image, active job. Rows self-update. |
| `GET /devices/{id}` | Device detail | Status panel, image selector, action buttons (power, flash), recent jobs. Controls gated by `capabilities`. |
| `GET /devices/{id}/console` | Console | Full-screen xterm.js terminal (see §11). |
| Fragment routes | e.g. `GET /ui/devices/{id}/row`, `/ui/jobs/{id}/card` | HTML partials returned to htmx swaps. |

### 12.2 Capability-driven controls

The dashboard fetches `GET /devices/{id}/capabilities` and renders only relevant controls — a PoE device shows no serial-relay power options; a device without a `serial` block shows no Console button. This is exactly the adaptivity stock Swagger UI cannot provide, and the reason the thin dashboard exists alongside it.

### 12.3 Live updates with htmx

**Fleet rows (light polling):**

```html
<tr hx-get="/ui/devices/dev-01/row"
    hx-trigger="load, every 5s"
    hx-swap="outerHTML">
  ...
</tr>
```

**Job progress (SSE, no polling):**

```html
<div hx-ext="sse" sse-connect="/jobs/{{ job.id }}/events">
  <div sse-swap="progress" hx-swap="innerHTML">
    <!-- step checklist re-rendered on each event -->
  </div>
</div>
```

**Triggering a flash:**

```html
<button hx-post="/devices/dev-01/flash"
        hx-target="#job-panel"
        hx-swap="innerHTML">
  Flash desired image
</button>
```

The POST returns the job card fragment (with its SSE subscription wired in), so clicking flash immediately shows a live-updating progress panel.

### 12.4 Swagger UI's role

`/docs` remains available and is the recommended surface for typed, ad-hoc operations and schema reference (every plugin config renders as a discriminated dropdown). The dashboard is the fleet-glance + one-click surface. Same API underneath; no design compromise on either side.

---

## 13. Directory Layout

```
device-manager/
├── app/
│   ├── main.py                  # FastAPI app, startup: load config, mount routers/static
│   ├── config.py                # YAML loading -> Device models (validated at load)
│   ├── api/
│   │   ├── devices.py           # device + status + capabilities + power + flash + images
│   │   ├── jobs.py              # job list/detail + SSE events
│   │   └── console.py           # WebSocket serial endpoint
│   ├── models/
│   │   ├── device.py            # Device, SerialConfig, DeviceStatus, DeviceCapabilities
│   │   ├── power.py             # PowerConfig union + variants
│   │   ├── flashing.py          # FlashingConfig union + variants
│   │   ├── vlan.py              # VlanConfig union + variants
│   │   ├── probe.py             # ProbeConfig union + variants
│   │   ├── image.py             # BootImage
│   │   └── job.py               # Job, JobStep
│   ├── plugins/
│   │   ├── base.py              # abstract interfaces
│   │   ├── registry.py          # PluginRegistry + registrations
│   │   ├── power/               # poe.py, network_socket.py, serial_relay.py
│   │   ├── flashing/            # netboot.py, fastboot.py, sd_mux.py
│   │   ├── vlan/                # switch_api.py, vxlan.py
│   │   └── probe/               # ssh_agent.py, serial_banner.py
│   ├── services/
│   │   ├── reconciler.py        # flash workflow orchestration
│   │   ├── job_manager.py       # asyncio job tracking + per-device locks
│   │   ├── serial_bridge.py     # SerialSession + SerialManager
│   │   └── artifactory.py       # image catalog + fetch
│   └── dashboard/
│       ├── routes.py            # page + fragment routes
│       ├── templates/
│       │   ├── base.html
│       │   ├── index.html       # fleet
│       │   ├── device.html      # detail
│       │   ├── console.html     # xterm.js
│       │   └── partials/        # row.html, job_card.html, controls.html
│       └── static/
│           ├── htmx.min.js
│           ├── sse.js           # htmx SSE extension
│           ├── xterm.js
│           ├── xterm.css
│           └── app.css
├── config/
│   ├── devices.yaml
│   └── images.yaml
├── udev/
│   └── 99-device-manager.rules
├── tests/
│   ├── test_models.py           # union validation, bad-config rejection
│   ├── test_plugins.py          # each plugin against its interface (mocked hw)
│   └── test_reconciler.py       # flash step sequencing
├── pyproject.toml
└── README.md
```

---

## 14. End-to-End Flows

### 14.1 Flash a device from the dashboard

1. Operator opens `/devices/dev-01`, picks image `rpi4-debian-12` in the selector → htmx `PUT /devices/dev-01/desired-image`.
2. Clicks **Flash** → htmx `POST /devices/dev-01/flash` → `202` + job card fragment (with SSE subscription).
3. Reconciler runs: `assign → power_off (PoE) → write (sd-mux: switch SD to host, dd image, switch back) → power_on → verify (ssh probe)`.
4. Each step emits SSE events; the job card re-renders a live checklist with the write percentage.
5. On success the fleet row updates `running_os = rpi4-debian-12`.

### 14.2 Serial console session

1. Operator opens `/devices/dev-01/console`.
2. Browser opens `WS /devices/dev-01/console`; controller lazily opens `/dev/devmgr/dev-01` in a shared `SerialSession`.
3. Keystrokes flow browser→serial; device output broadcasts serial→browser and renders in xterm.js.
4. A second operator can attach to the same session (read-write or read-only per policy).
5. On last detach (plus idle timeout) the port is released.

---

## 15. Deployment

- **Single controller node**, Linux, with the USB-serial adapters, power-control reachability, and flashing transports physically attached.
- Install udev rules (§7.3) for stable device paths; add the service user to `dialout` (or equivalent) for serial access.
- Run under systemd:

```ini
# /etc/systemd/system/device-manager.service
[Unit]
Description=Network Device Manager
After=network-online.target

[Service]
ExecStart=/opt/device-manager/.venv/bin/uvicorn app.main:app --host 0.0.0.0 --port 8000
WorkingDirectory=/opt/device-manager
Restart=on-failure
SupplementaryGroups=dialout

[Install]
WantedBy=multi-user.target
```

- Uvicorn serves REST, WebSocket, SSE, and static assets from one process. A reverse proxy (nginx/Caddy) is optional and useful only for TLS termination or auth.
- **Single worker** is required if serial sessions and the in-memory job store must be coherent (multiple workers would each open ports / hold separate job state). At this scale one worker is ample; scale vertically if ever needed.

---

## 16. Security Considerations

Power control and flashing are destructive, and the serial console is effectively root-on-console for many devices. Even as an internal lab tool:

- Bind to a trusted management network / VLAN, not a public interface.
- Add authentication in front (reverse-proxy SSO, or FastAPI dependency-based auth) before any mutating endpoint or the console WebSocket. WebSocket auth should validate a token on connect.
- Treat plugin configs and image paths as trusted operator input; validate/allow-list Artifactory paths to avoid writing arbitrary blobs.
- Log all mutating operations (who flashed/power-cycled what, when) — the job records are a natural audit trail; persist them (SQLite) if audit is required.

---

## 17. Dependencies

```toml
[project]
name = "device-manager"
requires-python = ">=3.12"
dependencies = [
    "fastapi",
    "uvicorn[standard]",
    "pydantic>=2",
    "jinja2",
    "pyyaml",
    "httpx",
    "pyserial-asyncio",
    "sse-starlette",
]

[project.optional-dependencies]
dev = ["pytest", "pytest-asyncio", "ruff", "mypy"]
```

Frontend assets (`htmx.min.js`, htmx SSE extension, `xterm.js`, `xterm.css`) are vendored into `app/dashboard/static/` — no npm, no build step.

---

## 18. Out of Scope / Future

- Fully dynamic runtime plugin discovery (start with the static discriminated union; §6.4).
- Durable job/audit history in SQLite (in-memory is fine initially).
- Multi-controller / horizontal scale (single node covers 4–10 devices comfortably).
- RBAC beyond a single operator role.
- Automatic reconciliation loops (current model is operator-triggered, not continuously self-healing).

---

## Appendix A — Answering the design-tension question

There is **no real trade-off** between an easily usable Swagger UI and clean plugin code. The apparent tension comes from over-abstraction: a single generic `POST /action` with an opaque `Dict[str, Any]` body is "DRY" but undocumented, unvalidated, and useless in Swagger. The design here avoids that by (1) typed discriminated-union configs per plugin, (2) explicit resource endpoints as the stable contract with plugin dispatch hidden beneath, and (3) a desired-vs-actual reconciliation model that maps cleanly onto the control-panel operations. All three improve both the UI and the code.

The **one genuine limitation** is that stock Swagger UI is static and API-wide: it's an excellent operation console but a poor fleet dashboard, and it can't adapt controls per device. That is precisely the gap the thin htmx dashboard and the `capabilities` endpoint fill — and building it requires no compromise to the API design, because the dashboard is just another client of the same well-typed API.
