# Screenpipe research: screen history as agent context

Researched 2026-09-19; revised to reflect the user's scope clarification. **Hash out the retrieval experience when we reach the [coding-agent milestone](roadmap.md#agent-assisted-recall-using-the-users-coding-agents). This is research, not an implementation commitment.**

“I had a meeting with xyz and he sent me something—what was it?”

That is the anchor use case. The user's coding agent searches Replay's OCR by person, topic, or approximate date, receives matching text with timestamps and moment IDs, and fetches relevant stored images plus adjacent moments. The images can supply context that flattened OCR loses. The agent uses that evidence to answer, citing moments the user can inspect in Replay.

The answer depends on the item or surrounding conversation having appeared on a recorded screen. Mentioning a meeting does not imply audio capture. This should work without first building automatic person, app, or project identity systems.

## The product boundary

**Replay supplies searchable screen history and relevant original images to fill the user's agent's context.** The agent interprets that evidence using its existing capabilities and the user's instructions. Drafting replies, producing reports or scripts, resuming tasks, workflow automation, and external actions belong entirely to that agent. They are not proposed Replay features, including for later milestones.

The prototype currently retains selected-monitor images, OCR text and geometry, and timestamps. The agent interface remains roadmap work. Structured app names, window titles, URLs, project identities, audio, clipboard, and keystrokes are not currently collected. Such clues may be visible in an image; they are not guaranteed metadata. Multiple apps may appear in one moment.

## Retrieval scenarios worth discussing

| User question | Context Replay should make available | Evidence limit |
| --- | --- | --- |
| “What did xyz send me after that meeting?” | OCR hits for names/topics, matching images, nearby moments showing a message, link, or attachment | The item must have been visible; do not invent an attachment's unseen contents. |
| “Where did Patrick and I discuss that invoice?” | Relevant moments across captured screens, including surrounding visible conversation | A person's name alone may be ambiguous. Missing history does not prove a conversation never happened. |
| “Which projects was I using Omakase in?” | Occurrences of the search term and images containing project/path clues | Omakase is an example search term, not a Replay component. No automatic project association is required. |
| “Why did we choose this approach, and where had I got to?” | Visible discussions, decisions, and the last relevant screen state | A proposal is not an accepted decision; historical evidence does not establish today's implementation. |
| “What was that error or documentation page I saw earlier?” | Matching text, original images, neighboring moments, and readable source clues | A URL or error may be incomplete. A captured page is historical evidence, not current truth. |

These are adaptations for Replay, not claims that Screenpipe or Replay has passed these scenarios in testing.

## What the sources contribute

The supplied [homepage](https://screenpipe.com/) and [Rewind comparison](https://screenpipe.com/blog/rewind-ai-alternative-2026) led to Screenpipe's [use-case gallery](https://screenpipe.com/use-cases). Their broader product and comparative claims were not independently validated. No installation, runtime evaluation, or video verification was performed.

The most relevant documentation is the [Claude Code integration guide](https://docs.screenpipe.com/claude-code): it describes searches and frame context that an existing agent can use to recover earlier information. [Agent memory](https://docs.screenpipe.com/agent-memory-workflow) also distinguishes usable historical context from restoring a model's hidden state. These are documented recipes, not our runtime proof.

Several broader recipes informed the research: [relationship follow-up](https://docs.screenpipe.com/relationship-follow-up), [engineering decisions](https://docs.screenpipe.com/engineering-decision-log), [incident reconstruction](https://docs.screenpipe.com/incident-reconstruction), and [research briefs](https://docs.screenpipe.com/research-brief). Their relevant contribution here is recovering supporting evidence and separating observation from inference.

Also reviewed: [project briefs](https://docs.screenpipe.com/daily-project-brief), [daily reviews](https://docs.screenpipe.com/daily-work-review), [SOP capture](https://docs.screenpipe.com/team-sop-capture), and [workflow handoffs](https://docs.screenpipe.com/workflow-handoff). Their artifact generation and automation are outside Replay's product boundary; they remain research references only.

## Questions for the agent milestone

- What minimal search, image-fetch, adjacent-moment, and open-in-Replay tools does an installed agent need?
- How should hits convey pending OCR, capture gaps, exclusions, expired history, and uncertain text?
- How much text and image context should a request retrieve, and how should the configured model/provider path be shown?
- How can citations reliably reopen the supporting moment with minimal keyboard friction?

Start with the “what did xyz send me?” question. Test whether the agent locates the correct visible evidence and leaves unsupported details unknown, including ambiguous names and incomplete coverage. Measure retrieval latency and resource impact. Screen content remains evidence, never instructions for the agent to obey.
