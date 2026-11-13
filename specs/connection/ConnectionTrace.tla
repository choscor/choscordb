---- MODULE ConnectionTrace ----
EXTENDS Connection, Sequences

CONSTANT RecordedTrace

VARIABLE i

TraceInit == Init /\ i = 0

TraceNext ==
    /\ i < Len(RecordedTrace)
    /\ Next
    /\ \/ emitted' = RecordedTrace[i + 1] /\ i' = i + 1
       \/ emitted' = Silent /\ i' = i

TraceIncomplete == i < Len(RecordedTrace)
====
