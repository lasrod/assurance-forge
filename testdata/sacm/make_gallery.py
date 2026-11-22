"""Writes the gallery fixtures into testdata/sacm/ (run once; the files are committed as written)."""
import sys
from xml.sax.saxutils import quoteattr

OUT = sys.argv[1]


def desc(eid, texts):
    if isinstance(texts, str):
        texts = [("en", texts)]
    values = "".join(f"<value lang={quoteattr(l)} content={quoteattr(t)} />" for l, t in texts)
    return f'<description xmi:id="d_{eid}"><content>{values}</content></description>'


def el(kind, eid, name, text=None, decl=None):
    d = f' assertionDeclaration="{decl}"' if decl else ""
    body = f"<name content={quoteattr(name)} />" + (desc(eid, text) if text else "")
    return f'    <argumentElement xmi:type="sacm:{kind}" xmi:id="{eid}"{d}>{body}</argumentElement>'


def goal(eid, name, text, decl=None):
    return el("Claim", eid, name, text, decl)


def strategy(eid, name, text):
    return el("ArgumentReasoning", eid, name, text)


def art(eid, name, text):
    return el("ArtifactReference", eid, name, text)


def rel(kind, eid, sources, target, reasoning=None, counter=False):
    r = f' reasoning="{reasoning}"' if reasoning else ""
    c = ' isCounter="true"' if counter else ""
    return f'    <argumentElement xmi:type="sacm:{kind}" xmi:id="{eid}" source="{sources}" target="{target}"{r}{c}><name content="{eid}" /></argumentElement>'


def inf(eid, sources, target, reasoning=None, counter=False):
    return rel("AssertedInference", eid, sources, target, reasoning, counter)


def ev(eid, sources, target):
    return rel("AssertedEvidence", eid, sources, target)


def ctx(eid, sources, target):
    return rel("AssertedContext", eid, sources, target)


def case(file, name, comment, lines):
    text = f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- {comment} -->
<sacm:AssuranceCasePackage xmlns:sacm="http://www.omg.org/spec/SACM/20220301" xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmi:id="case_{name}">
  <name content="{name}" />
  <argumentPackage xmi:id="argpkg_{name}">
    <name content="{name}Argument" />
""" + "\n".join(lines) + """
  </argumentPackage>
</sacm:AssuranceCasePackage>
"""
    with open(f"{OUT}/{file}", "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


# 1. Shared support and evidence, reached through branches of different depths.
case(
    "gallery-shared-support.sacm.xmi",
    "SharedSupport",
    "Gallery fixture: a goal and two solutions each supporting more than one parent, at different depths; Sn3 supports G2 directly and through G5.",
    [
        goal("G1", "SystemSafe", "The brake system is acceptably safe for public road use."),
        strategy("S1", "OverHazards", "Argument over each identified hazard."),
        goal("G2", "NoUnintendedBraking", "Unintended braking is acceptably unlikely."),
        goal("G3", "NoLossOfBraking", "Loss of braking is acceptably unlikely."),
        goal("G4", "ControllerCorrect", "The brake controller software meets its requirements."),
        goal("G5", "SensorsAdequate", "Wheel speed sensing is adequate for brake control."),
        goal("G6", "RedundancyWorks", "The redundant channel takes over within 50 ms."),
        art("Sn1", "UnitTests", "Controller unit test report UT-41."),
        art("Sn2", "HilCampaign", "Hardware-in-the-loop campaign HIL-7."),
        art("Sn3", "FmeaReport", "Sensor FMEA, revision C."),
        inf("i1", "G2 G3", "G1", reasoning="S1"),
        inf("i2", "G4 G5", "G2"),
        inf("i3", "G4 G6", "G3"),
        ev("e1", "Sn1", "G4"),
        ev("e2", "Sn2", "G4"),
        ev("e3", "Sn3", "G5"),
        ev("e4", "Sn2", "G6"),
        ev("e5", "Sn2", "G3"),
        ev("e6", "Sn3", "G2"),
    ],
)

# 2. Several context, assumption and justification attachments.
case(
    "gallery-attachments.sacm.xmi",
    "Attachments",
    "Gallery fixture: a goal and a strategy with several contexts, assumptions and justifications, and a context shared by two goals.",
    [
        goal("G1", "TopClaim", "The level crossing controller is acceptably safe."),
        art("C1", "SystemDefinition", "Controller as defined in SD-12, revision 3."),
        art("C2", "OperatingEnvironment", "Single-track rural crossings, line speed up to 120 km/h."),
        art("C3", "SafeDefinition", "Acceptably safe: tolerable hazard rate met for every hazard."),
        goal("A1", "TrainDetection", "Track circuits detect every approaching train.", "assumed"),
        goal("A2", "Maintenance", "Barriers are maintained to the published schedule.", "assumed"),
        goal("J1", "Standard", "EN 50129 requires a hazard-based argument for SIL 4.", "axiomatic"),
        strategy("S1", "OverHazards", "Argument over each hazard in the hazard log."),
        goal("J2", "LogComplete", "The hazard log was reviewed by an independent assessor.", "axiomatic"),
        art("C4", "HazardLog", "Hazard log HL-3, as baselined on 2026-09-01."),
        goal("A3", "NoNewHazards", "No hazard has been introduced since the baseline.", "assumed"),
        goal("G2", "BarriersClose", "Barriers close before a train reaches the crossing."),
        goal("G3", "WarningGiven", "Road users are warned before the barriers close."),
        art("C5", "WarningTime", "Warning time: at least 27 s before the train arrives."),
        art("Sn1", "TimingAnalysis", "Timing analysis TA-9."),
        art("Sn2", "FieldTrials", "Field trials FT-2, 400 closures."),
        ctx("x1", "C1 C2 C3", "G1"),
        ctx("x2", "A1 A2", "G1"),
        ctx("x3", "J1", "G1"),
        inf("i1", "G2 G3", "G1", reasoning="S1"),
        ctx("x4", "J2 C4 A3", "S1"),
        ctx("x5", "C5", "G2"),
        ctx("x6", "C5", "G3"),
        ev("e1", "Sn1", "G2"),
        ev("e2", "Sn2", "G3"),
    ],
)

# 3. Long names, long statements and unbroken identifiers.
LONG = (
    "The redundant brake controller channel detects every single-point fault of the primary channel, "
    "including stuck-at faults on the wheel speed inputs, corrupted CAN frames, watchdog expiry and "
    "loss of supply voltage, and takes over control within the fault tolerant time interval of 50 ms "
    "defined in the technical safety concept TSC-BRK-004 revision 7, section 4.3.2."
)
case(
    "gallery-long-text.sacm.xmi",
    "LongText",
    "Gallery fixture: long names, a long statement, an identifier with no break opportunity and a long unbroken token.",
    [
        goal("REQ_SYS_BRAKE_CONTROLLER_REDUNDANT_CHANNEL_MONITORING_0001",
             "RedundantChannelMonitoringDetectsEverySinglePointFaultOfThePrimaryChannel", LONG),
        strategy("S_ARGUMENT_OVER_EACH_FAULT_CLASS_IDENTIFIED_IN_THE_FMEDA",
                 "ArgumentOverFaultClasses", "Argument over each fault class identified in the FMEDA of the primary channel."),
        goal("G_STUCK", "StuckAtFaultsDetected", "Stuck-at faults on the wheel speed inputs are detected."),
        goal("G_CAN", "CorruptedFramesDetected", "Corrupted CAN frames are detected by the end-to-end protection profile."),
        art("C_URL", "Specification",
            "https://example.org/specifications/brake/technical-safety-concept/TSC-BRK-004/revision-7/section-4.3.2"),
        art("Sn_FAULT_INJECTION_CAMPAIGN_REPORT_FI_2026_0042_FINAL", "FaultInjectionCampaignReportFinal",
            "Fault injection campaign report FI-2026-0042, final issue, covering 12 400 injected faults."),
        art("Sn_E2E", "E2eAnalysis", "Analysis E2E-3."),
        goal("A_SHORT", "A", "x", "assumed"),
        inf("i1", "G_STUCK G_CAN", "REQ_SYS_BRAKE_CONTROLLER_REDUNDANT_CHANNEL_MONITORING_0001",
            reasoning="S_ARGUMENT_OVER_EACH_FAULT_CLASS_IDENTIFIED_IN_THE_FMEDA"),
        ctx("x1", "C_URL", "REQ_SYS_BRAKE_CONTROLLER_REDUNDANT_CHANNEL_MONITORING_0001"),
        ctx("x2", "A_SHORT", "G_STUCK"),
        ev("e1", "Sn_FAULT_INJECTION_CAMPAIGN_REPORT_FI_2026_0042_FINAL", "G_STUCK"),
        ev("e2", "Sn_E2E", "G_CAN"),
    ],
)

# 4. Japanese and mixed-language content.
case(
    "gallery-japanese.sacm.xmi",
    "Japanese",
    "Gallery fixture: statements in Japanese and English, Japanese mixed with Latin terms, and one statement only in English.",
    [
        goal("G1", "SystemSafe", [("ja", "自動運転システムは、定義された運行設計領域（ODD）において許容できる程度に安全である。"),
                                  ("en", "The automated driving system is acceptably safe within its defined ODD.")]),
        art("C1", "Odd", [("ja", "ODD：高速道路、晴天および小雨、最高速度 100 km/h。"),
                          ("en", "ODD: motorways, clear weather and light rain, up to 100 km/h.")]),
        strategy("S1", "OverHazards", [("ja", "ISO 26262 および ISO 21448（SOTIF）に基づき、識別された各ハザードについて論証する。"),
                                      ("en", "Argument over each identified hazard, following ISO 26262 and ISO 21448 (SOTIF).")]),
        goal("G2", "Perception", [("ja", "知覚機能は、ODD 内のすべての車両と歩行者を検出する。"),
                                  ("en", "Perception detects every vehicle and pedestrian within the ODD.")]),
        goal("G3", "Planning", [("ja", "経路計画は、ASIL-D の要求事項を満たす。"),
                                ("en", "Path planning meets its ASIL-D requirements.")]),
        goal("A1", "Map", [("ja", "高精度地図は最新である。"), ("en", "The HD map is up to date.")], "assumed"),
        art("Sn1", "Validation", [("ja", "走行試験報告書 VT-12（総走行距離 120 万 km）"),
                                  ("en", "Validation test report VT-12 (1.2 million km)")]),
        art("Sn2", "Review", [("en", "Independent design review DR-4, English only.")]),
        ctx("x1", "C1", "G1"),
        inf("i1", "G2 G3", "G1", reasoning="S1"),
        ctx("x2", "A1", "G3"),
        ev("e1", "Sn1", "G2"),
        ev("e2", "Sn2", "G3"),
    ],
)

# 5. Several roots and a cycle.
case(
    "gallery-roots-and-cycle.sacm.xmi",
    "RootsAndCycle",
    "Gallery fixture: two roots, a cycle of three goals reached from one of them, and a goal that supports itself.",
    [
        goal("R1", "FirstRoot", "The first top-level claim."),
        goal("R2", "SecondRoot", "A second top-level claim, sharing a premise with the first."),
        goal("G1", "SharedPremise", "A premise both roots rely on."),
        goal("Ca", "CycleA", "A claim that relies on B."),
        goal("Cb", "CycleB", "A claim that relies on C."),
        goal("Cc", "CycleC", "A claim that relies on A, closing the cycle."),
        goal("Gs", "SelfSupported", "A claim that supports itself."),
        art("Sn1", "Evidence", "Evidence for the shared premise."),
        inf("i1", "G1 Ca", "R1"),
        inf("i2", "G1 Gs", "R2"),
        inf("i3", "Cb", "Ca"),
        inf("i4", "Cc", "Cb"),
        inf("i5", "Ca", "Cc"),
        inf("i6", "Gs", "Gs"),
        ev("e1", "Sn1", "G1"),
    ],
)
print("written")
