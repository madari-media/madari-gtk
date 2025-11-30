gnome = import('gnome')

# Dependencies
gtk4_dep = dependency('gtk4', version: '>= 4.10')
libadwaita_dep = dependency('libadwaita-1', version: '>= 1.4')
json_glib_dep = dependency('json-glib-1.0')
libsoup_dep = dependency('libsoup-3.0')
mpv_dep = dependency('mpv', required: true)
epoxy_dep = dependency('epoxy', required: true)
egl_dep = dependency('egl', required: true)

madari_deps = [
  gtk4_dep,
  libadwaita_dep,
  json_glib_dep,
  libsoup_dep,
  mpv_dep,
  epoxy_dep,
  egl_dep,
]

# GResource compilation
madari_resources = gnome.compile_resources(
  'madari-resources',
  'madari.gresource.xml',
  c_name: 'madari'
)

# Stremio SDK sources
stremio_sources = files(
  'stremio/stremio_types.cpp',
  'stremio/stremio_parser.cpp',
  'stremio/stremio_client.cpp',
  'stremio/stremio_addon_service.cpp',
)

# Trakt integration sources
trakt_sources = files(
  'trakt/trakt_service.cpp',
)

madari_sources = [
  'main.cpp',
  'application.cpp',
  'application.hpp',
  'window.cpp',
  'window.hpp',
  'preferences_window.cpp',
  'preferences_window.hpp',
  'detail_view.cpp',
  'detail_view.hpp',
  'watch_history.cpp',
  'watch_history.hpp',
  stremio_sources,
  trakt_sources,
  madari_resources,
]

executable('madari', madari_sources,
  dependencies: madari_deps,
  install: true,
)

