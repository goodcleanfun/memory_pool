install:
	clib install --dev

test:
	@$(CC) $(CFLAGS) test.c -I src -I deps -o $@
	@./$@

.PHONY: test
