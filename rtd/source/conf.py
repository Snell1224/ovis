# Configuration file for the Sphinx documentation builder.

import os
import sys
from sphinx.ext import apidoc

# Path setup (add project root to sys.path)
sys.path.insert(0, os.path.abspath('../../ovis'))

# Dynamically generate .rst files from your package
def run_apidoc(_):
    """Generate .rst files dynamically from the 'ovis' package."""
    ignore_paths = ['rtd/source/conf.py']
    apidoc.main(['-f', '-o', 'rtd/source', 'ovis', *ignore_paths])

# Connect the function to Sphinx's event
def setup(app):
    app.connect('builder-inited', run_apidoc)

# -- Project information

project = 'OVIS-HPC'
copyright = '2024, Sandia National Laboratories and Open Grid Computing, Inc.'
author = 'SNL/OGC'

release = '0.1'
version = '0.1.0'

# -- General configuration

extensions = [
    'sphinx.ext.duration',
    'sphinx.ext.doctest',
    'sphinx.ext.autodoc',
    'sphinx.ext.autosummary',
    'sphinx.ext.intersphinx',
]

intersphinx_mapping = {
    'python': ('https://docs.python.org/3/', None),
    'sphinx': ('https://www.sphinx-doc.org/en/master/', None),

    # Link to the "apis" of the "hpc-ovis" project and subprojects
    "ovis-hpc": ("https://ovis-hpc-personal.readthedocs.io/en/latest/", None),
    "sos": ("https://ovis-hpc-personal.readthedocs.io/projects/sos/en/latest/", None),
    "maestro": ("https://ovis-hpc-personal.readthedocs.io/projects/maestro/en/latest/", None),
    "baler": ("https://ovis-hpc-personal.readthedocs.io/projects/baler/en/latest/", None),
    "ldms": ("https://ovis-hpc-personal.readthedocs.io/projects/ldms/en/latest/", None),

}
intersphinx_disabled_domains = ['std']
intersphinx_disabled_reftypes = ["*"]

templates_path = ['_templates']

# -- Options for HTML output

html_theme = 'sphinx_rtd_theme'
html_static_path = ['static']
html_logo = "https://github.com/ovis-hpc-personal/readthedocs/blob/main/docs/source/images/ovis-logo.png?raw=true"
html_theme_options = {
    'logo_only': True,
    'display_version': False,
}

# -- Options for EPUB output
epub_show_urls = 'footnote'
