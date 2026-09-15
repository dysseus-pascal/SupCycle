#pragma once

// Selbsttest der Zyklusrechnung, ausgefuehrt AUF DER UHR.
//
// Auf dem Baurechner steht kein C-Compiler - nur der ARM-Compiler der SDK.
// Ein Test, der nie laeuft, ist keiner; also laeuft er dort, wo ohnehin
// uebersetzt wird. Gebaut nur mit -DKI_SELFTEST; im ausgelieferten Paket ist
// kein Byte davon.
#ifdef KI_SELFTEST
int cycle_selftest_run(void);
#endif
