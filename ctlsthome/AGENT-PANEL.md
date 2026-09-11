# Optional embedded agent panel

This is the retained Pocket Agent surface inside Home, enabled only by the
existing `CTLST_LEGACY_AGENT=1` launch environment. It is **off by default**
and is not a mandatory agent service or a new core package dependency.
The optional terminal-based developer agent remains a separate workflow.

## Modern-UI prototype

The header groups identity and status; an activity indicator runs only while
a request is pending. Ask stays next to the prompt. Replies are complete,
selectable, wrapping text in a kinetic scroller rather than a two-line excerpt.
The scroller contains the quoted question, response, suggestions and any
proposed action; it cannot enlarge the surrounding panel. Suggestions wrap
into rows at narrow widths. Buttons have a 44px minimum height.

The quiet semantic card protects text from wallpaper/decorative visual noise.
Existing At a glance / Signal / Bloom selection is retained behind it and is
deliberately subdued. The palette still comes from the session's generated
theme; Ask and Run use the selected-text contrast role.

A proposed action has its own wrapping description and separate Cancel action
and Run action buttons. The button label is not model-generated. The frontend
stores the `action_id` accompanying that description and passes that exact
token to `ctlst-agent confirm TOKEN` or `cancel TOKEN`; it never relies on
another client's mutable pending-action file. Missing/malformed tokens cannot
enable Run. Starting another question or consuming an action discards the old
displayed token. The broker's allowlist, execution tiers, confirmation policy
and expiry rules are unchanged. This redesign does not authorize new actions.

Busy requests disable duplicate submissions. Missing helpers and failed
responses restore controls, stop the indicator and preserve the typed question
for correction/retry. Only a successful Ask clears it. Failed action requests
do not silently retry, and their displayed confirmation token is discarded.

## Input and configuration boundaries

The native prompt supports Enter to Ask; other controls use native GTK
activation. Moving focus from text entry to an agent response/control clears
the text-input ownership marker but keeps keyboard navigation in the panel.
Leaving Home or locking still releases prompt ownership through the existing
session path. Broker/client and keyboard-provider integration must be tested
separately; an inert callback test is not physical keyboard acceptance.

No new configuration format was introduced. Enablement and the inherited
portrait-first visibility behavior still belong to the legacy launch path;
landscape activation, full Home/grid integration, current-provider typing and
decorative GL modes remain acceptance work. A first-class dotfile enablement
option or extraction into a standalone companion should be decided explicitly,
not quietly installed by an appearance change.

## Evidence

The portable `tests/home-agent-test.c` builds the actual native content and
runs a temporary inert helper, without launching a broker, model, network,
call or SMS action. It checks 300–936px content widths at a 220px panel height,
scrolling, missing-helper/malformed-response recovery and exact Run/Cancel
arguments. `vm/clean-room/agent-ui.py` captures that content in light/dark
themes in a disposable ARM VM. These are content/adapter checks in an inert
host, **not full Home, keyboard-provider, GL-performance or release acceptance**.
Source contracts live in `tests/test_home_agent_ui.py`; both repository
variants retain their intended client/theme path differences.
