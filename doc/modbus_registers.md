# Wavin AHC 9000 Modbus Registers

Source: `doc/modbus_doc.doc` (same source material as `Wavin Modbus beskrivelse for AHC 9000 styreenhed_14082013.pdf` in related repo)

> ⚠️ Note: This is a scaffold generated before full table extraction from the source PDF/Word doc content. Populate/verify all register rows against the original document before production use.

## Register map (template)

| Address | Name | Function code | Access | Type | Length | Scale | Unit | Description | Notes |
|---:|---|---|---|---|---:|---:|---|---|---|
|  |  | 03/04/06/16 | R/W | int16/uint16/int32/float | 1/2 |  |  |  |  |

## Suggested groups

- Controller info / firmware
- Room temperature readings
- Floor temperature readings
- Setpoints and offsets
- Modes (comfort, setback, away)
- Relay/actuator outputs
- Alarm and fault registers
- Time/schedule related registers

## Data decoding notes

- Clarify whether addresses are documented as 0-based or 1-based.
- Clarify whether multi-register values are little-endian or big-endian.
- Validate signedness for temperature and offsets.
- Confirm scaling factors (e.g., `value / 10`).

## Validation checklist

- [ ] Every register has explicit access mode (R, W, R/W)
- [ ] Every numeric value has unit + scaling
- [ ] Function code compatibility verified
- [ ] Read/write behavior tested on hardware
