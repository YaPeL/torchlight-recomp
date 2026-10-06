"""Exact replay-only SPIR-V operand correction: tfetch result exponent word4→3.

No alpha/discard instructions are removed. Original captures remain untouched.
"""
import ast
import json
import struct
from pathlib import Path

root=Path('docs/bringup-artifacts/inventory-pixels')
reflection=json.loads((root/'broken-discard/reflection-6109-ShaderStage.Pixel.json').read_text())
raw=ast.literal_eval(reflection['rawBytes'])
words=list(struct.unpack('<%dI'%(len(raw)//4),raw))
instructions=[]
p=5
while p<len(words):
    size=words[p]>>16
    assert size
    instructions.append(words[p:p+size]);p+=size
constants={i[2]:i for i in instructions if i[0]&65535==43 and len(i)==4}
defs={i[2]:i for i in instructions if i[0]&65535 in (61,65,124)}
targets=[i for i in instructions if i[0]&65535==202 and constants[i[4]][3]==13 and constants[i[5]][3]==6]
assert len(targets)==1
extract=targets[0]
bitcast=defs[extract[3]];assert bitcast[0]&65535==124
load=defs[bitcast[3]];assert load[0]&65535==61
chain=defs[load[3]];assert chain[0]&65535==65
assert [constants[x][3] for x in chain[4:]]==[0,1,0]
next_id=words[3]
def fresh():
    global next_id
    result=next_id;next_id+=1;return result
index_type=constants[chain[-1]][1]
index3=next((k for k,v in constants.items() if v[1]==index_type and v[3]==3),None)
if index3 is None:
    index3=fresh()
    pos=next(n for n,i in enumerate(instructions) if i[0]&65535==54)
    instructions.insert(pos,[(4<<16)|43,index_type,index3,3])
new_chain=list(chain);new_chain[2]=fresh();new_chain[-2]=chain[-1];new_chain[-1]=index3
new_load=list(load);new_load[2]=fresh();new_load[3]=new_chain[2]
new_cast=list(bitcast);new_cast[2]=fresh();new_cast[3]=new_load[2]
pos=instructions.index(extract)
instructions[pos:pos]=[new_chain,new_load,new_cast]
extract[3]=new_cast[2]
patched=words[:5]+[w for i in instructions for w in i];patched[3]=next_id
(root/'quad-original.spv').write_bytes(raw)
(root/'quad-exponent-word3.spv').write_bytes(struct.pack('<%dI'%len(patched),*patched))
(root/'replay-operand-change.json').write_text(json.dumps({'original_word4_access_chain':chain,'new_word3_access_chain':new_chain,'new_load':new_load,'new_bitcast':new_cast,'corrected_exponent_extract':extract,'discard_instructions_unchanged':True},indent=2))
print('Patched only the result-exponent source; retained LOD word4 and all discard logic.')
