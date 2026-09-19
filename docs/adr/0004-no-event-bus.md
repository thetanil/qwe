# No event bus in the kernel

The kernel design doc called the event bus "the actual heart" of the plugin system (§4.4). After the other decisions, nothing uses it:
- Step plugins run in forked children (ADR-0001) and communicate only through `with:` inputs, step outputs and log bytes.
- Service plugins find each other through the kernel's service locator.
- Log fan-out is its own mechanism (ring, then log sink, then live consumers).
- The only example subscriber (`power:changed`) belonged to the device-manager service, which qwe is not.

So the kernel keeps the service locator and drops the bus. Step plugins are the spine of the plugin model, which resolves kernel doc §17.1.

## Consequences

- If `qwe serve` later needs broadcast events, a bus can be added then, as a service plugin or a kernel addition, without changing step plugins.
- Progress display comes from the log fan-out plus job and step state changes that the kernel reports directly.
