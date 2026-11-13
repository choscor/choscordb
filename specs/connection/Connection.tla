---- MODULE Connection ----
EXTENDS Naturals, TLC

Q == INSTANCE Query WITH state <- "Idle", last <- "None", failing <- FALSE

ConnectedIdle == "Connected { transaction_active: false }"
ConnectedInTransaction == "Connected { transaction_active: true }"
ProbedIdle == "Probed { transaction_active: false }"
ProbedInTransaction == "Probed { transaction_active: true }"
TransactionClosed == "TransactionChanged { active: false }"
TransactionOpened == "TransactionChanged { active: true }"

States == {"Connecting", "Probing", ConnectedIdle, ConnectedInTransaction, "Failed",
           "Disconnecting", "Disconnected"}
Events == {"Opened", "OpenFailed", ProbedIdle, ProbedInTransaction, "ProbeFailed",
           TransactionClosed, TransactionOpened, "Shutdown", "Closed"}

Connected(s) == s \in {ConnectedIdle, ConnectedInTransaction}
Opening(s) == s \in {"Connecting", "Probing"}
Ended(s) == s \in {"Failed", "Disconnected"}

Illegal == "Illegal"
Step(s, e) ==
    CASE s = "Connecting" /\ e = "Opened" -> "Probing"
      [] s = "Connecting" /\ e = "OpenFailed" -> "Failed"
      [] s = "Probing" /\ e = ProbedIdle -> ConnectedIdle
      [] s = "Probing" /\ e = ProbedInTransaction -> ConnectedInTransaction
      [] s = "Probing" /\ e = "ProbeFailed" -> "Failed"
      [] Opening(s) /\ e = "Shutdown" -> "Disconnected"
      [] Connected(s) /\ e = TransactionClosed -> ConnectedIdle
      [] Connected(s) /\ e = TransactionOpened -> ConnectedInTransaction
      [] Connected(s) /\ e = "Shutdown" -> "Disconnecting"
      [] s = "Disconnecting" /\ e = "Closed" -> "Disconnected"
      [] OTHER -> Illegal
Allowed(s, e) == Step(s, e) # Illegal
Transitions == {<<s, e, Step(s, e)>> : <<s, e>> \in {p \in States \X Events : Allowed(p[1], p[2])}}

CatalogSessions == {"Unsupported", "Closed", "Open", "RetryPending"}
ReadSession(transactionActive, catalog) ==
    IF transactionActive \/ catalog \in {"Unsupported", "RetryPending"} THEN "Query" ELSE "Catalog"
ReadSessions == {<<t, c, ReadSession(t, c)>> : t \in BOOLEAN, c \in CatalogSessions}

Slots == {"sql", "object"}
SlotStates == {"None", "Queued", "Running", "Cancelling"}
Causes == {"None", "Cancel", "Timeout"}
Reports == {"None", "Cancelled", "Timeout", "Disconnected", "Error"}
Working == {"Queued", "Running", "Cancelling"}
CompletedRows == "Completed { empty: false }"
CompletedEmpty == "Completed { empty: true }"
Silent == [kind |-> "Silent"]

VARIABLES conn, slot, cause, report, shown, catalog, emitted
vars == <<conn, slot, cause, report, shown, catalog, emitted>>

TransactionActive == conn = ConnectedInTransaction

TypeOK ==
    /\ conn \in States
    /\ slot \in [Slots -> SlotStates]
    /\ cause \in [Slots -> Causes]
    /\ report \in [Slots -> Reports]
    /\ shown \in [Slots -> Q!States]
    /\ catalog \in CatalogSessions

Init ==
    /\ conn = "Connecting"
    /\ slot = [x \in Slots |-> "None"]
    /\ cause = [x \in Slots |-> "None"]
    /\ report = [x \in Slots |-> "None"]
    /\ shown = [x \in Slots |-> "Idle"]
    /\ catalog \in {"Unsupported", "Closed"}
    /\ emitted = Silent

Move(e) == Allowed(conn, e) /\ conn' = Step(conn, e)

ConnectionSilently(e) ==
    /\ Move(e)
    /\ emitted' = Silent
    /\ UNCHANGED <<slot, cause, report, shown, catalog>>

ConnectionEmits(e, label) ==
    /\ Move(e)
    /\ emitted' = label
    /\ UNCHANGED <<slot, cause, report, shown, catalog>>

Open == ConnectionSilently("Opened")
OpenFail == ConnectionEmits("OpenFailed", [kind |-> "ConnectionFailed"])
Probe(t) == ConnectionEmits(IF t THEN ProbedInTransaction ELSE ProbedIdle,
                            [kind |-> "Connected", transaction |-> t])
ProbeFail == ConnectionEmits("ProbeFailed", [kind |-> "ConnectionFailed"])
ShutdownWhileOpening == Opening(conn) /\ ConnectionEmits("Shutdown", [kind |-> "Disconnected"])
ShutdownWhileConnected == Connected(conn) /\ ConnectionSilently("Shutdown")
Close ==
    /\ \A x \in Slots : slot[x] = "None"
    /\ ConnectionEmits("Closed", [kind |-> "Disconnected"])
TransactionChange(t) ==
    /\ slot["sql"] = "None"
    /\ ConnectionSilently(IF t THEN TransactionOpened ELSE TransactionClosed)
TransactionReport(t) ==
    /\ slot["sql"] = "Running"
    /\ ConnectionEmits(IF t THEN TransactionOpened ELSE TransactionClosed,
                       [kind |-> "TransactionState", active |-> t])

Show(x, value, next, nextCause, nextReport) ==
    /\ slot' = [slot EXCEPT ![x] = next]
    /\ cause' = [cause EXCEPT ![x] = nextCause]
    /\ report' = [report EXCEPT ![x] = nextReport]
    /\ shown' = [shown EXCEPT ![x] = IF next = "None" THEN "Idle" ELSE value]
    /\ emitted' = [kind |-> "QueryState", slot |-> x, state |-> value]
    /\ UNCHANGED <<conn, catalog>>

Submit(x) == ~Ended(conn) /\ conn # "Disconnecting" /\ slot[x] = "None" /\ report[x] = "None"
             /\ Show(x, "Queued", "Queued", "None", "None")
Start(x) == Connected(conn) /\ slot[x] = "Queued" /\ Show(x, "Running", "Running", "None", "None")
Complete(x) == Connected(conn) /\ slot[x] = "Running"
               /\ \E value \in {CompletedRows, CompletedEmpty} : Show(x, value, "None", "None", "None")
Fail(x) == slot[x] \in {"Queued", "Running"} /\ Show(x, "Failed", "None", "None", "Error")
Interrupt(x, why) ==
    /\ slot[x] \in Working
    /\ Show(x, "Cancelling", "Cancelling", IF cause[x] = "None" THEN why ELSE cause[x], "None")
Cancel(x) == Interrupt(x, "Cancel")
Timeout(x) == Interrupt(x, "Timeout")
Settle(x) ==
    /\ slot[x] = "Cancelling"
    /\ IF cause[x] = "Cancel"
       THEN Show(x, "Cancelled", "None", "None", "Cancelled")
       ELSE Show(x, "Failed", "None", "None", "Timeout")
Drain(x) ==
    /\ conn \in {"Disconnecting", "Failed", "Disconnected"}
    /\ slot[x] # "None"
    /\ Show(x, "Failed", "None", "None", "Disconnected")
Report(x) ==
    /\ report[x] # "None"
    /\ report' = [report EXCEPT ![x] = "None"]
    /\ emitted' = [kind |-> "QueryFailed", slot |-> x, error |-> report[x]]
    /\ UNCHANGED <<conn, slot, cause, shown, catalog>>

Catalog(next, label) ==
    /\ Connected(conn)
    /\ catalog' = next
    /\ emitted' = label
    /\ UNCHANGED <<conn, slot, cause, report, shown>>
CatalogOpen == ~TransactionActive /\ catalog = "Closed" /\ Catalog("Open", Silent)
CatalogOpenFail == ~TransactionActive /\ catalog = "Closed"
                   /\ Catalog("RetryPending", [kind |-> "OperationFailed"])
CatalogRetry == catalog = "RetryPending" /\ Catalog("Closed", Silent)
CatalogLost == catalog = "Open" /\ Catalog("Closed", Silent)
Read ==
    /\ TransactionActive \/ catalog # "Closed"
    /\ Catalog(catalog, [kind |-> "Read", session |-> ReadSession(TransactionActive, catalog)])

Progress(x) == Start(x) \/ Complete(x) \/ Fail(x) \/ Settle(x) \/ Drain(x) \/ Report(x)
Lifecycle == Open \/ OpenFail \/ (\E t \in BOOLEAN : Probe(t)) \/ ProbeFail \/ Close

Next ==
    \/ Lifecycle
    \/ ShutdownWhileOpening \/ ShutdownWhileConnected
    \/ \E t \in BOOLEAN : TransactionChange(t) \/ TransactionReport(t)
    \/ \E x \in Slots : Submit(x) \/ Cancel(x) \/ Timeout(x) \/ Progress(x)
    \/ CatalogOpen \/ CatalogOpenFail \/ CatalogRetry \/ CatalogLost \/ Read

Spec == Init /\ [][Next]_vars /\ WF_vars(Lifecycle) /\ \A x \in Slots : WF_vars(Progress(x))

ShownStateIsQueryState == \A x \in Slots : shown[x] \in Q!States
BusyNeutral == \A x \in Slots : Q!Busy(shown[x]) => Q!Tone(shown[x]) = "Neutral"
EmptyNeverFailed == \A x \in Slots : ~(Q!Body(shown[x]) = "Empty" /\ Q!Tone(shown[x]) = "Error")
CatalogFollowsTransaction ==
    emitted.kind = "Read" /\ TransactionActive => emitted.session = "Query"

QueryStatesFollowQueryMachine ==
    [][emitted'.kind = "QueryState" =>
         \E e \in Q!Events : Q!Step(shown[emitted'.slot], e) = emitted'.state]_vars
FailureNeverCompletes ==
    [][\A x \in Slots : cause[x] # "None" /\ emitted'.kind = "QueryState" /\ emitted'.slot = x
         => emitted'.state \notin {CompletedRows, CompletedEmpty}]_vars
CancelThroughCancelling ==
    [][emitted'.kind = "QueryState" /\ emitted'.state = "Cancelled"
         => slot[emitted'.slot] = "Cancelling"]_vars
RunsTerminate == \A x \in Slots : slot[x] \in Working ~> slot[x] = "None"
FailuresAreReported == \A x \in Slots : report[x] # "None" ~> report[x] = "None"
CancellingEnds == \A x \in Slots : slot[x] = "Cancelling" ~> slot[x] # "Cancelling"
ConnectingEnds == Opening(conn) ~> ~Opening(conn)
====
