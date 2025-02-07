install:
	clib install --dev

test:
	@$(CC) $(CFLAGS) test.c -I src -I deps $(LDFLAGS) -o $@
	@./$@

.PHONY: test
