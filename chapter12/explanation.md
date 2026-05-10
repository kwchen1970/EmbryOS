Description of Paging Implementation:
We defined a new macro for ease of coding, PTE_TO_PA(pte), which gets the pte’s physical address. 

We then updated vm_map, vm_is_mapped, vm_flush, vm_release, and vm_init to support the memory extension. We made the page table two levels rather than one.

The vm_map, vm_release, and vm_is_mapped functions are rewritten from one-level to two-level walks.

There are larger changes for vm_flush and vm_init. For vm_flush, before, the page table treated base as a L2 page table and inserted it as one entry into the parent_page_table (an L1 page table). Now, base is an L1 page table (as is the parent_page_table) so it reads base’s entries and copies them one-by-one into the parent_page_table. For vm_init, it now deliberately sets VM regions to invalid (instead of using 1:1 mappings) so they can be filled in later, and it also flushes the current process’s mappings at the end.

Additionally, vm_init, vm_map, and vm_flush all use sfence.vma instead of tlb_flush() for a more comprehensive clearing.

AI Tools Used: Claude, Github Copilot, ChatGPT (and Codex)

Explanation of AI Usage: 
We passed in the assignment description into AI to first generate a plan for the code implementation for the project, and then execute that plan. We debugged the resultant generated files collaboratively with AI, and further inspected on our own.

We also used AI to help us with writing this explanation.md by having it summarize sections of the code and differences from the original code to ensure our understanding was correct; we wrote everything here in our own words.