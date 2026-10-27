#!/usr/bin/env node
// dis8051 — minimal MCS-51/8051 disassembler for offline firmware archaeology.
// usage: dis8051.js file.bin [start-hex] ; linear disassembly from start.
'use strict';
const fs = require('fs');

// [mnemonic, bytes, operand-kind] — kind: 0 none, 1 imm8(#), 2 direct, 3 rel,
// 4 imm16, 5 a11 (ACALL/AJMP), 6 bit, 7 @Ri implied, 8 Rn implied
const T = {};
const put = (op, m, b, k) => (T[op] = { m, b, k });
put(0x00, 'NOP', 1, 0);
for (let i = 0; i < 8; i++) { put(0x01 + i * 0x20, 'AJMP', 2, 5); put(0x11 + i * 0x20, 'ACALL', 2, 5); }
put(0x02, 'LJMP', 3, 4); put(0x03, 'RR', 1, 9); put(0x04, 'INC', 1, 9);
put(0x05, 'INC', 2, 2); put(0x06, 'INC', 1, 7); put(0x07, 'INC', 1, 7);
for (let i = 0; i < 8; i++) put(0x08 + i, 'INC', 1, 8);
put(0x10, 'JBC', 3, 6); put(0x12, 'LCALL', 3, 4); put(0x13, 'RRC', 1, 9);
put(0x14, 'DEC', 1, 9); put(0x15, 'DEC', 2, 2); put(0x16, 'DEC', 1, 7);
put(0x17, 'DEC', 1, 7);
for (let i = 0; i < 8; i++) put(0x18 + i, 'DEC', 1, 8);
put(0x20, 'JB', 3, 6); put(0x22, 'RET', 1, 0); put(0x23, 'RL', 1, 9);
put(0x24, 'ADD', 2, 1); put(0x25, 'ADD', 2, 2); put(0x26, 'ADD', 1, 7);
put(0x27, 'ADD', 1, 7);
for (let i = 0; i < 8; i++) put(0x28 + i, 'ADD', 1, 8);
put(0x30, 'JNB', 3, 6); put(0x32, 'RETI', 1, 0); put(0x33, 'RLC', 1, 9);
put(0x34, 'ADDC', 2, 1); put(0x35, 'ADDC', 2, 2); put(0x36, 'ADDC', 1, 7);
put(0x37, 'ADDC', 1, 7);
for (let i = 0; i < 8; i++) put(0x38 + i, 'ADDC', 1, 8);
put(0x40, 'JC', 2, 3); put(0x42, 'ORL', 2, 21); put(0x43, 'ORL', 3, 22);
put(0x44, 'ORL', 2, 1); put(0x45, 'ORL', 2, 2); put(0x46, 'ORL', 1, 7);
put(0x47, 'ORL', 1, 7);
for (let i = 0; i < 8; i++) put(0x48 + i, 'ORL', 1, 8);
put(0x50, 'JNC', 2, 3); put(0x52, 'ANL', 2, 21); put(0x53, 'ANL', 3, 22);
put(0x54, 'ANL', 2, 1); put(0x55, 'ANL', 2, 2); put(0x56, 'ANL', 1, 7);
put(0x57, 'ANL', 1, 7);
for (let i = 0; i < 8; i++) put(0x58 + i, 'ANL', 1, 8);
put(0x60, 'JZ', 2, 3); put(0x62, 'XRL', 2, 21); put(0x63, 'XRL', 3, 22);
put(0x64, 'XRL', 2, 1); put(0x65, 'XRL', 2, 2); put(0x66, 'XRL', 1, 7);
put(0x67, 'XRL', 1, 7);
for (let i = 0; i < 8; i++) put(0x68 + i, 'XRL', 1, 8);
put(0x70, 'JNZ', 2, 3); put(0x72, 'ORL', 2, 6); put(0x73, 'JMP', 1, 10);
put(0x74, 'MOV', 2, 1); put(0x75, 'MOV', 3, 22); put(0x76, 'MOV', 2, 11);
put(0x77, 'MOV', 2, 11);
for (let i = 0; i < 8; i++) put(0x78 + i, 'MOV', 2, 12);
put(0x80, 'SJMP', 2, 3); put(0x82, 'ANL', 2, 6); put(0x83, 'MOVC', 1, 13);
put(0x84, 'DIV', 1, 14);
put(0x85, 'MOV', 3, 23); put(0x86, 'MOV', 2, 24); put(0x87, 'MOV', 2, 24);
for (let i = 0; i < 8; i++) put(0x88 + i, 'MOV', 2, 25);
put(0x90, 'MOV', 3, 15); put(0x92, 'MOV', 2, 16); put(0x93, 'MOVC', 1, 13);
put(0x94, 'SUBB', 2, 1); put(0x95, 'SUBB', 2, 2); put(0x96, 'SUBB', 1, 7);
put(0x97, 'SUBB', 1, 7);
for (let i = 0; i < 8; i++) put(0x98 + i, 'SUBB', 1, 8);
put(0xa0, 'ORL', 2, 17); put(0xa2, 'MOV', 2, 16); put(0xa3, 'INC', 1, 18);
put(0xa4, 'MUL', 1, 14); put(0xa5, 'DB', 1, 0); put(0xa6, 'MOV', 2, 24);
put(0xa7, 'MOV', 2, 24);
for (let i = 0; i < 8; i++) put(0xa8 + i, 'MOV', 2, 25);
put(0xb0, 'ANL', 2, 17); put(0xb2, 'CPL', 2, 6); put(0xb3, 'CPL', 1, 19);
put(0xb4, 'CJNE', 3, 26); put(0xb5, 'CJNE', 3, 27); put(0xb6, 'CJNE', 3, 28);
put(0xb7, 'CJNE', 3, 28);
for (let i = 0; i < 8; i++) put(0xb8 + i, 'CJNE', 3, 29);
put(0xc0, 'PUSH', 2, 2); put(0xc2, 'CLR', 2, 6); put(0xc3, 'CLR', 1, 19);
put(0xc4, 'SWAP', 1, 9); put(0xc5, 'XCH', 2, 2); put(0xc6, 'XCH', 1, 7);
put(0xc7, 'XCH', 1, 7);
for (let i = 0; i < 8; i++) put(0xc8 + i, 'XCH', 1, 8);
put(0xd0, 'POP', 2, 2); put(0xd2, 'SETB', 2, 6); put(0xd3, 'SETB', 1, 19);
put(0xd4, 'DA', 1, 9); put(0xd5, 'DJNZ', 3, 30); put(0xd6, 'XCHD', 1, 7);
put(0xd7, 'XCHD', 1, 7);
for (let i = 0; i < 8; i++) put(0xd8 + i, 'DJNZ', 2, 31);
put(0xe0, 'MOVX', 1, 32); put(0xe2, 'MOVX', 1, 33); put(0xe3, 'MOVX', 1, 33);
put(0xe4, 'CLR', 1, 9); put(0xe5, 'MOV', 2, 2); put(0xe6, 'MOV', 1, 7);
put(0xe7, 'MOV', 1, 7);
for (let i = 0; i < 8; i++) put(0xe8 + i, 'MOV', 1, 8);
put(0xf0, 'MOVX', 1, 34); put(0xf2, 'MOVX', 1, 35); put(0xf3, 'MOVX', 1, 35);
put(0xf4, 'CPL', 1, 9); put(0xf5, 'MOV', 2, 2); put(0xf6, 'MOV', 1, 7);
put(0xf7, 'MOV', 1, 7);
for (let i = 0; i < 8; i++) put(0xf8 + i, 'MOV', 1, 8);

const SFR = { 0x80: 'P0', 0x81: 'SP', 0x82: 'DPL', 0x83: 'DPH', 0x87: 'PCON',
  0x88: 'TCON', 0x89: 'TMOD', 0x8a: 'TL0', 0x8b: 'TL1', 0x8c: 'TH0',
  0x8d: 'TH1', 0x90: 'P1', 0x98: 'SCON', 0x99: 'SBUF', 0xa8: 'IE',
  0xa9: 'SADDR', 0xb8: 'IP', 0xc8: 'T2CON', 0xca: 'RCAP2L', 0xcb: 'RCAP2H',
  0xcd: 'TL2', 0xce: 'TH2', 0xd0: 'PSW', 0xe0: 'ACC', 0xf0: 'B' };
const dname = (d) => SFR[d] || ('0x' + d.toString(16).padStart(2, '0'));
const RN = ['R0', 'R1', 'R2', 'R3', 'R4', 'R5', 'R6', 'R7'];

const [file, startArg] = process.argv.slice(2);
const b = fs.readFileSync(file);
const start = startArg ? parseInt(startArg, 16) : 0;

for (let pc = start; pc < b.length;) {
  const op = b[pc];
  const t = T[op] || { m: '.DB', b: 1, k: -1 };
  let args = '';
  const imm = (d) => '#0x' + d.toString(16).padStart(2, '0');
  const dir = (d) => dname(d);
  const rel = (d) => '0x' + ((pc + t.b + (d < 128 ? d : d - 256)) & 0xffff).toString(16);
  switch (t.k) {
    case 1: args = 'A, ' + imm(b[pc + 1]); break;
    case 2: args = 'A, ' + dir(b[pc + 1]); break;
    case 3: args = rel(b[pc + 1]); break;
    case 4: args = '0x' + (b[pc + 1] << 8 | b[pc + 2]).toString(16); break;
    case 5: { const hi = op >> 5; args = '0x' + ((hi << 8 | b[pc + 1]) & 0x7ff).toString(16); break; }
    case 6: args = '0x' + b[pc + 1].toString(16).padStart(2, '0') + (t.b === 3 ? ', ' + rel(b[pc + 2]) : ''); break;
    case 7: args = '@R' + (op & 1); break;
    case 8: args = RN[op & 7]; break;
    case 9: args = 'A'; break;
    case 10: args = '@A+DPTR'; break;
    case 11: args = '@R' + (op & 1) + ', ' + imm(b[pc + 1]); break;
    case 12: args = RN[op & 7] + ', ' + imm(b[pc + 1]); break;
    case 13: case 14: args = 'AB'; break;
    case 15: args = 'DPTR, #0x' + (b[pc + 1] << 8 | b[pc + 2]).toString(16); break;
    case 16: args = 'C, 0x' + b[pc + 1].toString(16).padStart(2, '0'); break;
    case 17: args = 'C, /0x' + b[pc + 1].toString(16).padStart(2, '0'); break;
    case 18: args = 'DPTR'; break;
    case 19: args = 'C'; break;
    case 21: args = dir(b[pc + 1]) + ', A'; break;
    case 22: args = dir(b[pc + 1]) + ', ' + imm(b[pc + 2]); break;
    case 23: { const d1 = b[pc + 1], d2 = b[pc + 2]; args = dir(d2) + ', ' + dir(d1); break; }
    case 24: args = dir(b[pc + 1]) + ', @R' + (op & 1); break;
    case 25: args = dir(b[pc + 1]) + ', ' + RN[op & 7]; break;
    case 26: args = 'A, ' + imm(b[pc + 1]) + ', ' + rel(b[pc + 2]); break;
    case 27: args = 'A, ' + dir(b[pc + 1]) + ', ' + rel(b[pc + 2]); break;
    case 28: args = '@R' + (op & 1) + ', ' + imm(b[pc + 1]) + ', ' + rel(b[pc + 2]); break;
    case 29: args = RN[op & 7] + ', ' + imm(b[pc + 1]) + ', ' + rel(b[pc + 2]); break;
    case 30: args = dir(b[pc + 1]) + ', ' + rel(b[pc + 2]); break;
    case 31: args = RN[op & 7] + ', ' + rel(b[pc + 1]); break;
    case 32: args = 'A, @DPTR'; break;
    case 33: args = 'A, @R' + (op & 1); break;
    case 34: args = '@DPTR, A'; break;
    case 35: args = '@R' + (op & 1) + ', A'; break;
    default: args = '';
  }
  const bytes = hexa(b.slice(pc, pc + t.b));
  console.log(pc.toString(16).padStart(4, '0') + '  ' + bytes.padEnd(9) + t.m.padEnd(6) + args);
  pc += t.b;
}

function hexa(u) { return [...u].map(x => x.toString(16).padStart(2, '0')).join(' '); }
