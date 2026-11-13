---- MODULE GridEdit ----
EXTENDS Naturals, Sequences, TLC

Phases == {"Clean", "Pending", "Planning", "Applying", "Failed"}
Transactions == {"Inactive", "Active", "Ending"}
Name(e, t) == "GridEditState { edits: " \o e \o ", transaction: " \o t \o " }"
States == {Name(e, t) : e \in Phases, t \in Transactions}
Events == {"Edit", "Plan", "Planned", "PlanFailed", "Apply", "Applied", "ApplyFailed",
           "Discard", "Begin", "Commit", "Rollback", "Ended", "EndFailed", "Disconnect"}
EditResults == {"Planned", "PlanFailed", "Applied", "ApplyFailed"}
TransactionResults == {"Ended", "EndFailed"}

Illegal == <<"Illegal", "Illegal">>
Move(e, t, ev) ==
    CASE ev = "Edit" /\ e \in {"Clean", "Pending", "Failed"} -> <<"Pending", t>>
      [] ev = "Plan" /\ e = "Pending" -> <<"Planning", t>>
      [] ev = "Planned" /\ e = "Planning" -> <<"Pending", t>>
      [] ev = "PlanFailed" /\ e = "Planning" -> <<"Failed", t>>
      [] ev = "Apply" /\ e = "Pending" /\ t = "Inactive" -> <<"Applying", t>>
      [] ev = "Applied" /\ e = "Applying" -> <<"Clean", t>>
      [] ev = "ApplyFailed" /\ e = "Applying" -> <<"Failed", t>>
      [] ev = "Discard" /\ e \in {"Pending", "Failed"} -> <<"Clean", t>>
      [] ev = "Begin" /\ t = "Inactive" /\ e # "Applying" -> <<e, "Active">>
      [] ev \in {"Commit", "Rollback"} /\ t = "Active" /\ e # "Applying" -> <<e, "Ending">>
      [] ev = "Ended" /\ t = "Ending" -> <<e, "Inactive">>
      [] ev = "EndFailed" /\ t = "Ending" -> <<e, "Active">>
      [] ev = "Disconnect" -> <<IF e \in {"Planning", "Applying"} THEN "Failed" ELSE e, "Inactive">>
      [] OTHER -> Illegal

VARIABLES edits, transaction, failing
vars == <<edits, transaction, failing>>

StepPair(e, t, ev) == Move(e, t, ev)
Transitions == {<<Name(e, t), ev, Name(StepPair(e, t, ev)[1], StepPair(e, t, ev)[2])>> :
                <<e, t, ev>> \in {x \in Phases \X Transactions \X Events : StepPair(x[1], x[2], x[3]) # Illegal}}

Busy(e, t) == e \in {"Planning", "Applying"} \/ t = "Ending"
Tone(e, t) == CASE Busy(e, t) -> "Neutral"
                [] e = "Failed" -> "Error"
                [] OTHER -> "Neutral"
Body(e, t) == "None"
Presentation == {<<Name(e, t), Tone(e, t), Busy(e, t), Body(e, t), "Unchanged", FALSE>> :
                 e \in Phases, t \in Transactions}

TypeOK == edits \in Phases /\ transaction \in Transactions /\ failing \in BOOLEAN
Init == edits = "Clean" /\ transaction = "Inactive" /\ failing = FALSE
Fire(ev) ==
    /\ Move(edits, transaction, ev) # Illegal
    /\ edits' = Move(edits, transaction, ev)[1]
    /\ transaction' = Move(edits, transaction, ev)[2]
    /\ failing' = IF ev \in {"PlanFailed", "ApplyFailed"} \/ (ev = "Disconnect" /\ edits \in {"Planning", "Applying"})
                  THEN TRUE ELSE IF ev \in {"Edit", "Discard"} THEN FALSE ELSE failing
Next == \E ev \in Events : Fire(ev)
SettleEdits == \E ev \in EditResults : Fire(ev)
SettleTransaction == \E ev \in TransactionResults : Fire(ev)
Spec == Init /\ [][Next]_vars /\ WF_vars(SettleEdits) /\ WF_vars(SettleTransaction)

BusyNeutral == Busy(edits, transaction) => Tone(edits, transaction) = "Neutral"
EmptyNeverFailed == ~(Body(edits, transaction) = "Empty" /\ Tone(edits, transaction) = "Error")
FailureIsNeverApplied == failing => edits # "Clean"
ApplyNeedsNoTransaction == [][edits' = "Applying" /\ edits # "Applying" => transaction = "Inactive"]_vars
NoTransactionDuringApply == edits = "Applying" => transaction = "Inactive"
PlanningEnds == edits = "Planning" ~> edits # "Planning"
ApplyingEnds == edits = "Applying" ~> edits # "Applying"
EndingEnds == transaction = "Ending" ~> transaction # "Ending"
====
