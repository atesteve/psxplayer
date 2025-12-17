.globl _ZN12MMAPR3000Bus13read_mem_implIhEET_Pv
.globl _ZN12MMAPR3000Bus13read_mem_implItEET_Pv
.globl _ZN12MMAPR3000Bus13read_mem_implIjEET_Pv
.globl _ZN12MMAPR3000Bus14write_mem_implIhEEvPvT_
.globl _ZN12MMAPR3000Bus14write_mem_implItEEvPvT_
.globl _ZN12MMAPR3000Bus14write_mem_implIjEEvPvT_

.section .text

// unsigned char MMAPR3000Bus::read_mem_impl<unsigned char>(void*)
_ZN12MMAPR3000Bus13read_mem_implIhEET_Pv:
    movzbl (%rdi), %eax
    ret

// unsigned short MMAPR3000Bus::read_mem_impl<unsigned short>(void*)
_ZN12MMAPR3000Bus13read_mem_implItEET_Pv:
    movzwl (%rdi), %eax
    ret

// unsigned int MMAPR3000Bus::read_mem_impl<unsigned int>(void*)
_ZN12MMAPR3000Bus13read_mem_implIjEET_Pv:
    mov    (%rdi), %eax
    ret

// void MMAPR3000Bus::write_mem_impl<unsigned char>(void*, unsigned char)
_ZN12MMAPR3000Bus14write_mem_implIhEEvPvT_:
    mov    %sil, (%rdi)
    ret

// void MMAPR3000Bus::write_mem_impl<unsigned short>(void*, unsigned short)
_ZN12MMAPR3000Bus14write_mem_implItEEvPvT_:
    mov    %si, (%rdi)
    ret

// void MMAPR3000Bus::write_mem_impl<unsigned int>(void*, unsigned int)
_ZN12MMAPR3000Bus14write_mem_implIjEEvPvT_:
    mov    %esi, (%rdi)
    ret
