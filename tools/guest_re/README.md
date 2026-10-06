Static reading of the guest code on the image from `tools/xex/xex_dump.py`; FUNCS is `generated/` (or a list of addresses) for the function boundaries.
`gdis.py IMAGE FUNCS 821C4058` disassembles a function (needs capstone); `xref.py IMAGE FUNCS const|call 820DF730` finds who builds a constant or calls an address.
`rtti_vtable.py IMAGE FUNCS Ogre::D3D9RenderSystem [--anchor STRING] [--dump]` finds a class's vtables from its MSVC RTTI, where they are installed and which slot each anchor string uses.
`pc_match.py PC_EXE IMAGE FUNCS PC_ADDR...` ranks guest functions that may correspond to a function of the PC executable (`reference/pc/Torchlight.exe`, read only) by the string literals both reference, their own and their callees'; a lead to confirm in the code, not evidence by itself.
