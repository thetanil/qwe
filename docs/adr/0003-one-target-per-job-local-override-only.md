# One target per job; a step may override it only to run on the operator host

A job has exactly one target. That gives one ControlMaster per job, and it makes a workflow easy to read: everything a job does happens on one machine. Some work, such as creating a short-lived GitHub runner registration token with a credential that must never leave the operator's PC, has to happen on the operator host within a job that otherwise targets a remote controller. So a step may say `on: local`, and no other override is allowed. Steps pass values to each other only through step outputs, never through a shared working directory or runtime environment. That's what allows a `local` step and a remote step to be in the same job.

## Considered options

- **Any per-step target.** Rejected. Steps in one job would span several remote machines, and the one-connection-per-job model would be lost.
- **The plugin decides where its commands go.** Rejected. It hides which machine is being changed.
- **Split the work into two jobs connected by job outputs.** Rejected. Outputs are scoped to a job, and passing secret outputs between jobs isn't worth the complexity for an hour-long token.
