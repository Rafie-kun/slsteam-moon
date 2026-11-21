# Outils de reverse (x86_64)

Scripts Python (stdlib seule + `objdump`) qui ont servi à retrouver les locators de
`src/patterns_x86_64.hpp` dans le client 64 bits. Ils **trouvent** ; l'audit
`make test-patterns-x86_64` **vérifie**. À lancer depuis ce dossier.

Cible par défaut : `~/.local/share/Steam/steamrt64/steamclient.so` (`rev.PATH`). Le client
chargé par Steam 64 bits est celui-là, pas `linux64/steamclient.so`. UI : `steamrt64/steamui.so`.

| Script | Rôle |
|---|---|
| `rev.py` | `Elf` : sections, PT_LOAD, bornes de fonctions via `.eh_frame_hdr`, `find(pattern)`, `disasm` (objdump) |
| `rev32.py` | même chose pour l'ELF i386 (`ubuntu12_32/steamclient.so`), pierre de Rosette |
| `rtti.py` | relocations RELATIVE (`rel`) : valeur des pointeurs de vtables/typeinfos |
| `build_ti.py` | écrit `ti.pkl` (typeinfos, vtables) ; à relancer pour un autre binaire |
| `mapnames.py` | noms des méthodes par slot des classes `IClient*Map` (chaînes référencées) |
| `xref.py` | références `lea rip+disp` vers une chaîne, fonction englobante |
| `scan.py` | compte les matches des signatures d'un `patterns.cpp` dans un binaire |

## Méthodes qui ont marché

1. **Méthodes virtuelles** : l'ordre est celui des `IClient*Map` (proxys IPC) ; vtable de la
   classe d'implémentation (`5CUser` base `11IClientUser` à +8080, etc.) ; thunks `sub rdi,imm; jmp`.
2. **Dispatchers IPC** : forme `call; mov eax,[rbp-x]; cmp eax,ROOT`, racine ≈ racine i386.
3. **Chaînes de log/VPROF** (hors `.rodata`, autre section de données) : la fonction qui charge
   toutes les chaînes ; départage par nombre d'appelants directs.
4. **Pierre de Rosette i386** : le pattern Moon matche une fois sur le client i386 ; les chaînes
   VPROF (base GOT PIC) désignent la fonction ; mêmes nombres d'appelants en x64 (170, 2, 5).

Les chemins sont ceux de la machine echo ; adapter `rev.PATH` / `rev32.P32`.
