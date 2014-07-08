PYUIC=pyuic4

all: simwin_ui.py

%_ui.py: %.ui
	$(PYUIC) --from-imports -o $@ $<

clean:
	rm -f simwin_ui.py
