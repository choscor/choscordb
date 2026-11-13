---- MODULE ProfileTest ----
EXTENDS Naturals, TLC

States == {"Idle", "Testing", "Succeeded", "Failed", "AwaitingTrust", "Trusting", "Trusted"}
Events == {"Test", "Passed", "TestFailed", "HostKeyUnknown", "Approve", "Approved",
           "ApprovalFailed", "Reject", "Edit"}
Working == {"Testing", "Trusting"}
Results == {"Passed", "TestFailed", "HostKeyUnknown", "Approved", "ApprovalFailed"}
Restartable == {"Idle", "Succeeded", "Failed", "Trusted"}

Illegal == "Illegal"
Step(s, e) ==
    CASE e = "Test" /\ s \in Restartable -> "Testing"
      [] e = "Passed" /\ s = "Testing" -> "Succeeded"
      [] e = "TestFailed" /\ s = "Testing" -> "Failed"
      [] e = "HostKeyUnknown" /\ s = "Testing" -> "AwaitingTrust"
      [] e = "Approve" /\ s = "AwaitingTrust" -> "Trusting"
      [] e = "Approved" /\ s = "Trusting" -> "Trusted"
      [] e = "ApprovalFailed" /\ s = "Trusting" -> "Failed"
      [] e = "Reject" /\ s = "AwaitingTrust" -> "Idle"
      [] e = "Edit" /\ s \in Restartable \cup {"AwaitingTrust"} -> "Idle"
      [] OTHER -> Illegal
Allowed(s, e) == Step(s, e) # Illegal
Transitions == {<<s, e, Step(s, e)>> : <<s, e>> \in {p \in States \X Events : Allowed(p[1], p[2])}}

Busy(s) == s \in Working
Tone(s) == CASE s \in {"Succeeded", "Trusted"} -> "Success"
             [] s = "Failed" -> "Error"
             [] OTHER -> "Neutral"
Presentation == {<<s, Tone(s), Busy(s), "None", "Unchanged", FALSE>> : s \in States}

VARIABLES state, failing
vars == <<state, failing>>
TypeOK == state \in States /\ failing \in BOOLEAN
Init == state = "Idle" /\ failing = FALSE
Fire(e) ==
    /\ Allowed(state, e)
    /\ state' = Step(state, e)
    /\ failing' = IF e \in {"TestFailed", "ApprovalFailed"} THEN TRUE
                  ELSE IF e \in {"Test", "Edit"} THEN FALSE ELSE failing
Next == \E e \in Events : Fire(e)
Settle == Busy(state) /\ \E e \in Results : Fire(e)
Spec == Init /\ [][Next]_vars /\ WF_vars(Settle)

BusyNeutral == Busy(state) => Tone(state) = "Neutral"
FailureNeverSucceeds == failing => Tone(state) # "Success"
TrustNeedsApproval == [][state' = "Trusted" => state = "Trusting"]_vars
TestingEnds == state = "Testing" ~> state # "Testing"
TrustingEnds == state = "Trusting" ~> state # "Trusting"
====
