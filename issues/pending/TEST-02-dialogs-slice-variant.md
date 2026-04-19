# TEST-02 — functional test for messages.dialogsSlice

## Gap
US-04 acceptance:
> Correctly unwraps `messages.dialogsSlice` vs `messages.dialogs` vs
> `messages.dialogsNotModified`.

Current functional coverage (`test_read_path.c test_dialogs_*`) only
exercises the plain `messages.dialogs` constructor.

## Scope
1. Responder for `messages.getDialogs` replies with
   `messages.dialogsSlice#71e094f3 count:int dialogs:Vector<Dialog>
   messages:Vector<Message> chats:Vector<Chat> users:Vector<User>`.
2. Assert the domain layer returns the count separately from the
   returned batch size.

## Acceptance
- `domain_get_dialogs` parses the slice variant and surfaces
  `total_count`.
- Tests green; coverage of `src/domain/read/dialogs.c` slice branch
  rises.
