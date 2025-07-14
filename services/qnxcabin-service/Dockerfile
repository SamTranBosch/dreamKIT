# Use a base image, such as Python's image.
FROM python:3.11.8-slim

ARG TARGETPLATFORM
ARG BUILDPLATFORM

# Set the working directory inside the container.
WORKDIR /app

# Copy the requirements file to leverage Docker cache.
COPY requirements.txt .

# Install the Python dependencies.
RUN pip install --no-cache-dir -r requirements.txt

# Copy the rest of the application code into the container.
# COPY . .

# Copy application files into the container
COPY ./src/config.json ./
COPY ./src/main.py ./
# COPY ./src/example/*.py ./

# Precompile main.py to generate the .pyc file and remove the original .py file.
# The -f flag forces the regeneration of pyc files.
RUN python -m compileall -f main.py && \
    rm main.py
# RUN find . -name "*.py" -delete

# Copy the startup script
COPY start.sh .

# Make the startup script executable
RUN chmod +x start.sh

# Set the entry point of the container to the startup script.
CMD [ "./start.sh" ]
