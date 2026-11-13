---- MODULE Export ----
EXTENDS Naturals, TLC

States == {"Idle", "Submitting", "Running", "Cancelling", "Succeeded", "Cancelled", "Failed"}
Events == {"Submit", "Start", "Cancel", "Finish", "Fail", "CancelSettled", "Reset"}
Working == {"Submitting", "Running", "Cancelling"}
Settled == {"Succeeded", "Cancelled", "Failed"}
Results == {"Start", "Finish", "Fail", "CancelSettled"}

Illegal == "Illegal"
Step(s, e) ==
    CASE e = "Submit" /\ s \in {"Idle"} \cup Settled -> "Submitting"
      [] e = "Start" /\ s = "Submitting" -> "Running"
      [] e = "Cancel" /\ s \in Working -> "Cancelling"
      [] e = "Finish" /\ s \in {"Running", "Cancelling"} -> "Succeeded"
      [] e = "Fail" /\ s \in Working -> "Failed"
      [] e = "CancelSettled" /\ s = "Cancelling" -> "Cancelled"
      [] e = "Reset" /\ s \in Settled -> "Idle"
      [] OTHER -> Illegal
Allowed(s, e) == Step(s, e) # Illegal
Transitions == {<<s, e, Step(s, e)>> : <<s, e>> \in {p \in States \X Events : Allowed(p[1], p[2])}}

Busy(s) == s \in Working
Tone(s) == CASE s = "Succeeded" -> "Success" [] s = "Failed" -> "Error" [] OTHER -> "Neutral"
Presentation == {<<s, Tone(s), Busy(s), "None", "Unchanged", FALSE>> : s \in States}

VARIABLES state, last, failing
vars == <<state, last, failing>>
TypeOK == state \in States /\ last \in Events \cup {"None"} /\ failing \in BOOLEAN
Init == state = "Idle" /\ last = "None" /\ failing = FALSE
Fire(e) ==
    /\ Allowed(state, e)
    /\ state' = Step(state, e)
    /\ last' = e
    /\ failing' = IF e = "Fail" THEN TRUE ELSE IF e = "Submit" THEN FALSE ELSE failing
Next == \E e \in Events : Fire(e)
Settle == Busy(state) /\ \E e \in Results : Fire(e)
Spec == Init /\ [][Next]_vars /\ WF_vars(Settle)

BusyNeutral == Busy(state) => Tone(state) = "Neutral"
FailureNeverSucceeds == failing => state # "Succeeded"
CancelThroughCancelling ==
    [][/\ state' = "Cancelled" => state = "Cancelling"
       /\ last' = "Cancel" => state' = "Cancelling"]_vars
ExportsTerminate == Busy(state) ~> state \in Settled
CancellingEnds == state = "Cancelling" ~> state # "Cancelling"
====
