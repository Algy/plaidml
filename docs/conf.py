# -*- coding: utf-8 -*-
#
# Configuration file for the Sphinx documentation builder.
#
# This file does only contain a selection of the most common options. For a
# full list see the documentation:
# http://www.sphinx-doc.org/en/stable/config

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here.

import os
import sys

# We assume that docs/conf.py is located in the top level of the repository.
repo_root = os.path.dirname(os.path.dirname(__file__))
sys.path.insert(0, os.path.abspath(repo_root))

from recommonmark.parser import CommonMarkParser
from recommonmark.transform import AutoStructify

# -- Project information -----------------------------------------------------

project = u'PlaidML'
copyright = u'2019, Intel Corporation'
author = u'Intel Corporation'

# License specifics see LICENSE of component

# The version info for the project you're documenting, acts as replacement for
# |version| and |release|, also used in various other places throughout the
# built documents.
#
# The short X.Y version.
version = '0.5'

# The Documentation full version, including alpha/beta/rc tags. Some features
# available in the latest code will not necessarily be documented first
release = '0.5.0'

# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
#
# This is also used if you do content translation via gettext catalogs.
# Usually you set "language" from the command line for these cases.
language = 'en'

#   'da', 'de', 'en', 'es', 'fi', 'fr', 'hu', 'it', 'ja'
#   'nl', 'no', 'pt', 'ro', 'ru', 'sv', 'tr'

# -- General configuration ---------------------------------------------------

# If your documentation needs a minimal Sphinx version, state it here.
#
# needs_sphinx = '1.0'

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.inheritance_diagram',
    'sphinx.ext.mathjax',
    'sphinx.ext.viewcode',
    'sphinx.ext.graphviz',
    'sphinx.ext.githubpages',
    'sphinx.ext.napoleon',
    'sphinx.ext.autosummary',
]

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# The suffix(es) of source filenames.
# You can specify multiple suffix as a list of string:
#
source_suffix = ['.rst', '.md']

source_parsers = {
    '.md': CommonMarkParser,
}

# The master toctree document.
master_doc = 'index'

# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
#
# This is also used if you do content translation via gettext catalogs.
# Usually you set "language" from the command line for these cases.
language = None

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path .
exclude_patterns = [u'_build', 'Thumbs.db', '.DS_Store', 'venv']

# The name of the Pygments (syntax highlighting) style to use.
pygments_style = 'sphinx'

# -- Options for HTML output -------------------------------------------------

html_short_title = "PlaidML"


# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
html_theme = 'ngraph_theme'

html_logo = 'ngraph_theme/static/favicon.ico'

# The name of an image file (within the static path) to use as favicon of the
# docs.  This file should be a Windows icon file (.ico) being 16x16 or 32x32
# pixels large.
html_favicon = 'ngraph_theme/static/favicon.ico'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['ngraph_theme/static']

# Add any paths that contain custom themes here, relative to this directory.
html_theme_path = ["."]


# Custom sidebar templates, must be a dictionary that maps document names
# to template names.
#
# The default sidebars (for documents that don't match any pattern) are
# defined by theme itself.  Builtin themes are using these templates by
# default: ``['localtoc.html', 'relations.html', 'sourcelink.html',
# 'searchbox.html']``.
#
# html_sidebars = {}

html_sidebars = {
    '**': [
        'relations.html',  # needs 'show_related': True theme option to display
        'searchbox.html',
    ]
}

# -- Options for HTMLHelp output ---------------------------------------------

# Output file base name for HTML help builder.
htmlhelp_basename = 'PlaidMLdoc'

# -- Options for LaTeX output ------------------------------------------------

latex_elements = {
    # The paper size ('letterpaper' or 'a4paper').
    #
    # 'papersize': 'letterpaper',

    # The font size ('10pt', '11pt' or '12pt').
    #
    # 'pointsize': '10pt',

    # Additional stuff for the LaTeX preamble.
    #
    # 'preamble': '',

    # Latex figure (float) alignment
    #
    # 'figure_align': 'htbp',
}

# Grouping the document tree into LaTeX files. List of tuples
# (source start file, target name, title,
#  author, documentclass [howto, manual, or own class]).
latex_documents = [
    (master_doc, 'PlaidML.tex', u'PlaidML Documentation', u'Intel Corporation', 'manual'),
]

# -- Options for manual page output ------------------------------------------

# One entry per manual page. List of tuples
# (source start file, name, description, authors, manual section).
man_pages = [(master_doc, 'plaidml', u'PlaidML Documentation', [author], 1)]

# -- Options for Texinfo output ----------------------------------------------

# Grouping the document tree into Texinfo files. List of tuples
# (source start file, target name, title, author,
#  dir menu entry, description, category)
texinfo_documents = [
    (master_doc, 'PlaidML', u'PlaidML Documentation', author, 'PlaidML',
     'One line description of project.', 'Miscellaneous'),
]

# -- Extension configuration -------------------------------------------------

plantuml = os.getenv('PLANTUMLSH')

# The Napoleon extension allows google style doc comments, but it also uses
# numpy style too, by default; for consistency, we'll use only google style.
napoleon_numpy_docstring = False

# The rtype option just adds noise; disable it.
napoleon_use_rtype = False

# The generate mode will recursively produce stub pages for everything listed
# in an autosummary; this lets us document all the stuff inside plaidml without
# having to explicitly list each item. It doesn't work on the package itself,
# though, so we still have to list the contents of 'plaidml' itself inside the
# 'plaidml.rst' file; everything underneath that is produced into the api/
# subdirectory as needed.
autosummary_generate = True


def setup(app):
    app.add_config_value(
        'recommonmark_config',
        {
            'auto_toc_tree_section': 'Contents',
            # This feature has been deprecated, and, indeed, recommonmark will
            # choke on inter-document links if it's left enabled.
            'enable_auto_doc_ref': False,
        },
        True)
    app.add_transform(AutoStructify)
