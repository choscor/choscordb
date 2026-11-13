---- MODULE Query ----
EXTENDS Naturals, TLC

CompletedRows == "Completed { empty: false }"
CompletedEmpty == "Completed { empty: true }"
FinishRows == "Finish { empty: false }"
FinishEmpty == "Finish { empty: true }"

States == {"Idle", "Connecting", "Queued", "Running", "Cancelling",
           CompletedRows, CompletedEmpty, "Failed", "Cancelled", "Disconnected"}
Events == {"Connect", "SessionReady", "ConnectFailed", "Submit", "Start", "Fetch",
           "Cancel", "TimedOut", FinishRows, FinishEmpty, "Fail", "CancelSettled",
           "Disconnect", "Reset"}
Progress == {"SessionReady", "ConnectFailed", "Start", "TimedOut", FinishRows,
             FinishEmpty, "Fail", "CancelSettled"}
Failures == {"ConnectFailed", "Cancel", "TimedOut", "Fail", "Disconnect"}
Fresh == {"Connect", "Submit", "Reset"}

Busy(s) == s \in {"Connecting", "Queued", "Running", "Cancelling"}
Completed(s) == s \in {CompletedRows, CompletedEmpty}
Settled == {CompletedRows, CompletedEmpty, "Failed", "Cancelled", "Disconnected"}
Restartable == {"Idle"} \cup Settled
Working == {"Queued", "Running", "Cancelling"}

Illegal == "Illegal"
Step(s, e) ==
    CASE e = "Connect" /\ s \in Restartable -> "Connecting"
      [] e = "SessionReady" /\ s = "Connecting" -> "Idle"
      [] e = "ConnectFailed" /\ s = "Connecting" -> "Failed"
      [] e = "Submit" /\ s \in Restartable \ {"Disconnected"} -> "Queued"
      [] e = "Start" /\ s = "Queued" -> "Running"
      [] e = "Fetch" /\ Completed(s) -> "Running"
      [] e \in {"Cancel", "TimedOut"} /\ s \in Working -> "Cancelling"
      [] e = "Cancel" /\ s = "Failed" -> "Failed"
      [] e = FinishRows /\ s = "Running" -> CompletedRows
      [] e = FinishEmpty /\ s = "Running" -> CompletedEmpty
      [] e = "Fail" /\ s \in {"Idle", "Failed", "Disconnected"} \cup Working \cup {CompletedRows, CompletedEmpty} -> "Failed"
      [] e = "CancelSettled" /\ s = "Cancelling" -> "Cancelled"
      [] e = "Disconnect" /\ s = "Failed" -> "Failed"
      [] e = "Disconnect" /\ s # "Failed" -> "Disconnected"
      [] e = "Reset" /\ s \in Restartable -> "Idle"
      [] OTHER -> Illegal

Allowed(s, e) == Step(s, e) # Illegal
Transitions == {<<s, e, Step(s, e)>> : <<s, e>> \in {p \in States \X Events : Allowed(p[1], p[2])}}

Tone(s) == CASE Busy(s) -> "Neutral"
             [] Completed(s) -> "Success"
             [] s = "Failed" -> "Error"
             [] OTHER -> "Neutral"
Body(s) == CASE s = CompletedEmpty -> "Empty"
             [] s \in {CompletedRows, "Running", "Cancelling"} -> "Content"
             [] OTHER -> "None"
Pane(s) == CASE s = "Failed" -> "Messages"
             [] s \in {"Queued", "Running", CompletedRows, CompletedEmpty} -> "Grid"
             [] OTHER -> "Unchanged"
Toast(s) == FALSE
Presentation == {<<s, Tone(s), Busy(s), Body(s), Pane(s), Toast(s)>> : s \in States}

VARIABLES state, last, failing
vars == <<state, last, failing>>

TypeOK == state \in States /\ last \in Events \cup {"None"} /\ failing \in BOOLEAN

Init == state = "Idle" /\ last = "None" /\ failing = FALSE

Fire(e) ==
    /\ Allowed(state, e)
    /\ state' = Step(state, e)
    /\ last' = e
    /\ failing' = IF e \in Fresh \/ e = "Fetch" THEN FALSE
                  ELSE IF e \in Failures THEN TRUE ELSE failing

Next == \E e \in Events : Fire(e)
Settle == Busy(state) /\ \E e \in Progress : Fire(e) /\ state' # state
Spec == Init /\ [][Next]_vars /\ WF_vars(Settle)

EmptyNeverFailed == ~(Body(state) = "Empty" /\ Tone(state) = "Error")
BusyNeutral == Busy(state) => Tone(state) = "Neutral"
FailureNeverCompletes == failing => ~Completed(state)
FailedShowsMessages == state = "Failed" => Pane(state) = "Messages" /\ ~Toast(state)

CancelThroughCancelling ==
    [][/\ state' = "Cancelled" => state = "Cancelling"
       /\ last' \in {"Cancel", "TimedOut"} /\ state \in Working => state' = "Cancelling"]_vars
RunsTerminate == state \in Working ~> state \in Settled
CancellingEnds == state = "Cancelling" ~> state # "Cancelling"
ConnectingEnds == state = "Connecting" ~> state # "Connecting"
====
