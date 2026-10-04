//===-- MiniTPUPseudos.td - codegen-only markers (spike) -----*- tablegen -*-===//
//
// Hand-written glue: no ISA fact. These never reach an encoder.
//
//===----------------------------------------------------------------------===//

// Reorder mode (gate G4): the exporter dissolves every placed bundle into free
// instructions, except a bundle that holds a pinned op (descriptor, control, and
// by asm.MACHINE["movable"] policy the M and S slots). Such a bundle is one
// issue unit: this marker closes it, naming how many instructions before it
// belong to it, and carries the bundle's placed delay like DELAY does. The
// delay-folding pass turns it into a DELAY.
def DELAY_GROUP : MiniTPUInst_Delay<(outs), (ins uimm7_0_127:$members, uimm7_0_127:$n),
    "delay.group", "$members, $n"> {
  let Itinerary = II_DELAY;
  let isCodeGenOnly = 1;
  let isPseudo = 1;
  bits<7> n;
  let delay = {n{6-0}};
}
