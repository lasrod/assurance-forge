# Confidence Panel Prototype

The Confidence panel is an experimental Inspector feature for assigning session-only confidence information to a selected assurance case element. It is intended as a future feature showcase for richer assurance reasoning, not as a persisted SACM confidence model.

## Scope

The prototype supports two local input modes:

- Direct value: a single confidence value from `0.0` to `1.0`.
- Opinion triangle: a Jøsang-style subjective opinion selector with belief, disbelief, and uncertainty values.

Opinion values are normalized so that:

```text
belief + disbelief + uncertainty = 1.0
```

The projected confidence value is calculated as:

```text
belief + base_rate * uncertainty
```

The default base rate is `0.5`.

## Prototype Limits

Confidence values are currently stored only in runtime UI state. They are not serialized to SACM, saved in project metadata, propagated through the graph, or used by validation and review rules.

## Manual Test Checklist

- Select an element and confirm the Confidence panel appears below Element Properties.
- Enable and disable confidence without changing any other element properties.
- Switch between Direct value and Opinion triangle modes.
- In Direct value mode, move the slider and confirm the final confidence readout updates from `0.0` to `1.0`.
- In Opinion triangle mode, click the top vertex and confirm uncertainty is `1.0`.
- Click the lower-left vertex and confirm disbelief is `1.0`.
- Click the lower-right vertex and confirm belief is `1.0`.
- Click near the center and confirm belief, disbelief, and uncertainty are approximately equal.
- Drag the belief, disbelief, and uncertainty bars and confirm the marker moves with the adjusted values.
- Drag outside the triangle and confirm the marker remains inside the triangle.
- Confirm belief, disbelief, and uncertainty stay normalized to `1.0`.
- Change the base rate and confirm projected confidence updates.
- Select another element and confirm each element keeps separate confidence state for the session.
- Resize the Inspector and confirm labels, sliders, bars, and the marker remain readable.