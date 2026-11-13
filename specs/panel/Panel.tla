---- MODULE Panel ----
EXTENDS Naturals, TLC

States == {"Idle", "Loading", "Ready", "Empty", "NoMatch", "Failed", "Disconnected",
           "Incomplete", "Unsupported"}
Events == {"Load", "LoadedRows", "LoadedNone", "LoadedPartial", "LoadUnsupported",
           "LoadFailed", "FilterMatched", "FilterMissed", "Disconnect"}
Results == {"LoadedRows", "LoadedNone", "LoadedPartial", "LoadUnsupported", "LoadFailed"}
Outcome == [LoadedRows |-> "Ready", LoadedNone |-> "Empty", LoadedPartial |-> "Incomplete",
            LoadUnsupported |-> "Unsupported", LoadFailed |-> "Failed"]

Illegal == "Illegal"
Step(s, e) ==
    CASE e = "Load" -> "Loading"
      [] e \in Results /\ s = "Loading" -> Outcome[e]
      [] e = "FilterMatched" /\ s = "NoMatch" -> "Ready"
      [] e = "FilterMatched" /\ s # "NoMatch" -> s
      [] e = "FilterMissed" /\ s = "Ready" -> "NoMatch"
      [] e = "FilterMissed" /\ s # "Ready" -> s
      [] e = "Disconnect" /\ s = "Failed" -> "Failed"
      [] e = "Disconnect" /\ s # "Failed" -> "Disconnected"
      [] OTHER -> Illegal
Allowed(s, e) == Step(s, e) # Illegal
Transitions == {<<s, e, Step(s, e)>> : <<s, e>> \in {p \in States \X Events : Allowed(p[1], p[2])}}

Busy(s) == s = "Loading"
Tone(s) == CASE s = "Failed" -> "Error"
             [] s \in {"Ready", "Empty"} -> "Success"
             [] OTHER -> "Neutral"
Body(s) == CASE s \in {"Ready", "Incomplete"} -> "Content"
             [] s \in {"Empty", "Unsupported", "Disconnected"} -> "Empty"
             [] s = "NoMatch" -> "NoMatch"
             [] s = "Failed" -> "FailedPlaceholder"
             [] OTHER -> "None"
Toast(s) == s = "Failed"
Presentation == {<<s, Tone(s), Busy(s), Body(s), "Unchanged", Toast(s)>> : s \in States}

VARIABLES state, failed
vars == <<state, failed>>

TypeOK == state \in States /\ failed \in BOOLEAN
Init == state = "Idle" /\ failed = FALSE
Fire(e) ==
    /\ Allowed(state, e)
    /\ state' = Step(state, e)
    /\ failed' = IF e = "LoadFailed" THEN TRUE ELSE IF e = "Load" THEN FALSE ELSE failed
Next == \E e \in Events : Fire(e)
Finish == \E e \in Results : Fire(e)
Spec == Init /\ [][Next]_vars /\ WF_vars(Finish)

EmptyNeverFailed == ~(Body(state) = "Empty" /\ Tone(state) = "Error")
BusyNeutral == Busy(state) => Tone(state) = "Neutral"
FailureNeverShowsEmpty == failed => state = "Failed" /\ Body(state) = "FailedPlaceholder"
FailureShowsToast == state = "Failed" => Toast(state)
NoMatchIsNotEmpty == state = "NoMatch" => Body(state) # "Empty"
LoadingEnds == state = "Loading" ~> state # "Loading"
====
