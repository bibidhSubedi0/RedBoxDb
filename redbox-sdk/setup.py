from setuptools import setup, find_packages

setup(
    name="redboxdb",                # The name on PyPI (pip install redboxdb)
    version="1.0.1",
    description="Python client for the RedBox Vector Database",
    long_description=open("README.md", encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    author="Bibidh Subedi",
    url="https://github.com/bibidhSubedi0/RedBoxDb",
    packages=find_packages(),       # Finds the 'redboxdb' folder automatically
    install_requires=[
        "numpy",                    # Auto-installs numpy for the user
    ],
    include_package_data=True,
    package_data={
        'redboxdb': ['*.exe'],  # Include all .exe files inside the redboxdb folder
    },
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ],
    python_requires='>=3.6',
)