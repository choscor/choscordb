---- MODULE Updater ----
EXTENDS Naturals, TLC

States == {"Idle", "Checking", "UpToDate", "Available", "Downloading", "ReadyToInstall",
           "Installing", "Cancelling", "Cancelled", "Failed"}
Events == {"Check", "NoUpdate", "Found", "Download", "Downloaded", "Install", "Cancel",
           "CancelSettled", "Fail", "Dismiss", "Staged"}
Working == {"Checking", "Downloading", "Installing", "Cancelling"}
Results == {"NoUpdate", "Found", "Downloaded", "CancelSettled", "Fail", "Staged"}
Restartable == {"Idle", "UpToDate", "Available", "Cancelled", "Failed"}

Illegal == "Illegal"
Step(s, e) ==
    CASE e = "Check" /\ s \in Restartable \cup {"ReadyToInstall"} -> "Checking"
      [] e = "Staged" /\ s \in {"Checking", "UpToDate", "Available", "Failed", "Cancelled"}
            -> "ReadyToInstall"
      [] e = "NoUpdate" /\ s = "Checking" -> "UpToDate"
      [] e = "Found" /\ s = "Checking" -> "Available"
      [] e = "Download" /\ s = "Available" -> "Downloading"
      [] e = "Downloaded" /\ s = "Downloading" -> "ReadyToInstall"
      [] e = "Install" /\ s = "ReadyToInstall" -> "Installing"
      [] e = "Cancel" /\ s \in {"Checking", "Downloading", "Cancelling"} -> "Cancelling"
      [] e = "CancelSettled" /\ s = "Cancelling" -> "Cancelled"
      [] e = "Fail" /\ s \in Working -> "Failed"
      [] e = "Dismiss" /\ s \in Restartable \cup {"ReadyToInstall"} -> "Idle"
      [] OTHER -> Illegal
Allowed(s, e) == Step(s, e) # Illegal
Transitions == {<<s, e, Step(s, e)>> : <<s, e>> \in {p \in States \X Events : Allowed(p[1], p[2])}}

Busy(s) == s \in Working
Tone(s) == CASE s \in {"UpToDate", "ReadyToInstall"} -> "Success"
             [] s = "Failed" -> "Error"
             [] OTHER -> "Neutral"
Body(s) == IF s = "Failed" THEN "FailedPlaceholder" ELSE "None"
Toast(s) == s = "Failed"
Presentation == {<<s, Tone(s), Busy(s), Body(s), "Unchanged", Toast(s)>> : s \in States}

VARIABLES state, last, failing
vars == <<state, last, failing>>
TypeOK == state \in States /\ last \in Events \cup {"None"} /\ failing \in BOOLEAN
Init == state = "Idle" /\ last = "None" /\ failing = FALSE
Fire(e) ==
    /\ Allowed(state, e)
    /\ state' = Step(state, e)
    /\ last' = e
    /\ failing' = IF e \in {"Cancel", "Fail"} THEN TRUE ELSE IF e \in {"Check", "Staged"} THEN FALSE ELSE failing
Next == \E e \in Events : Fire(e)
Settle == Busy(state) /\ \E e \in Results : Fire(e)
Spec == Init /\ [][Next]_vars /\ WF_vars(Settle)

BusyNeutral == Busy(state) => Tone(state) = "Neutral"
EmptyNeverFailed == state = "Failed" => Body(state) = "FailedPlaceholder" /\ Toast(state)
FailureNeverSucceeds == failing => Tone(state) # "Success"
CancelThroughCancelling ==
    [][/\ state' = "Cancelled" => state = "Cancelling"
       /\ last' = "Cancel" => state' = "Cancelling"]_vars
WorkEnds == Busy(state) ~> ~Busy(state)
CancellingEnds == state = "Cancelling" ~> state # "Cancelling"
CheckingEnds == state = "Checking" ~> state # "Checking"
====
