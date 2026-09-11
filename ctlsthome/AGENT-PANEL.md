# Experimental embedded agent panel

Home contains an experimental frontend enabled by `CTLST_LEGACY_AGENT=1`.
It is disabled by default. This repository does not install a supported agent
backend or model service; enabling the frontend alone does not provide one.

## Frontend behavior

The panel provides a prompt, Ask action, selectable scrolling responses and
suggestion buttons. Enter in the prompt submits a request. Native GTK controls
support pointer and keyboard activation.

Busy requests disable duplicate submissions. Failed requests preserve the
question for retry and restore controls. Successful requests clear the prompt.
Moving focus out of the prompt releases text-entry ownership while retaining
panel navigation focus.

## Action interface

Proposed actions include a description and separate Run/Cancel controls.
The frontend stores the displayed `action_id` and passes that exact token to
`ctlst-agent confirm TOKEN` or `ctlst-agent cancel TOKEN`.
Missing or malformed tokens disable Run. A new question, consumed action or
failed action request discards the token; action requests are not retried
automatically.

This frontend is not an authorization boundary. Any backend must independently
validate requests, apply its execution policy and enforce confirmation expiry.

## Limitations

There is no supported dotfile enablement interface, complete landscape/Home-grid
integration or validated keyboard-provider workflow for this panel. Decorative
rendering modes and full-session behavior are experimental. Keep the default
disabled for ordinary shell installations.
