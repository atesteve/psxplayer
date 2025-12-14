.globl _ZN12MMAPR3000Bus13read_mem_implIhEET_PVv
.globl _ZN12MMAPR3000Bus13read_mem_implItEET_PVv
.globl _ZN12MMAPR3000Bus13read_mem_implIjEET_PVv
.globl _ZN12MMAPR3000Bus14write_mem_implIhEEvPVvT_
.globl _ZN12MMAPR3000Bus14write_mem_implItEEvPVvT_
.globl _ZN12MMAPR3000Bus14write_mem_implIjEEvPVvT_

.section .text

// unsigned char MMAPR3000Bus::read_mem_impl<unsigned char>(void volatile*)
_ZN12MMAPR3000Bus13read_mem_implIhEET_PVv:
    movzbl (%rdi), %eax
    ret

// unsigned short MMAPR3000Bus::read_mem_impl<unsigned short>(void volatile*)
_ZN12MMAPR3000Bus13read_mem_implItEET_PVv:
    movzwl (%rdi), %eax
    ret

// unsigned int MMAPR3000Bus::read_mem_impl<unsigned int>(void volatile*)
_ZN12MMAPR3000Bus13read_mem_implIjEET_PVv:
    mov    (%rdi), %eax
    ret

// void MMAPR3000Bus::write_mem_impl<unsigned char>(void volatile*, unsigned char)
_ZN12MMAPR3000Bus14write_mem_implIhEEvPVvT_:
    mov    %dl, (%rdi)
    ret

// void MMAPR3000Bus::write_mem_impl<unsigned short>(void volatile*, unsigned short)
_ZN12MMAPR3000Bus14write_mem_implItEEvPVvT_:
    mov    %dx, (%rdi)
    ret

// void MMAPR3000Bus::write_mem_impl<unsigned int>(void volatile*, unsigned int)
_ZN12MMAPR3000Bus14write_mem_implIjEEvPVvT_:
    mov    %edx, (%rdi)
    ret
