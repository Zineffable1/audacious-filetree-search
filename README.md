# audacious-filetree-search
A modification of the search-tool-qt plugin from audacious-plugins
https://github.com/audacious-media-player/audacious-plugins

It creates a searchable filetree that updates the current playlist (bar the Library playlist) as you single click on folders
Folders can be double clicked to expand/collapse or the user can single click the arrows.

I haven't figured out how to keep the last track playing when the playlist changes - I hope someone does.


I made this on Arch Linux (KDE Plasma 6 Wayland), all I had to do was copy the filetree-search-qt.so file to /usr/lib/audacious/General/ and enable the plugin.
e.g.(Running a terminal from the same folder as the .so file) sudo cp filetree-search-qt.so /usr/lib/audacious/General/
Not sure how this will go for other users.


<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/318f79c6-ee60-48bc-855f-4480f7ea59e2" />
