import requests
from constants import api_endpoint, save_path, img_width, img_height, refresh_time_s
import os
import time

# return image uuids from immich server
def get_image_list():
    try:
        result = requests.get(url=f"{api_endpoint}/images")
        result.raise_for_status()
        return result.json()["uuid"]

    except Exception as e:
        print(f"Failed to get image list: {e}")
        return []


# return image uuids from local files
def get_file_list():
    return [os.path.splitext(f)[0] for f in os.listdir(save_path) if os.path.isfile(os.path.join(save_path, f)) and os.path.splitext(f)[1] == ".jpg"]


def save_image(uuid):
    try:
        result = requests.get(url=f"{api_endpoint}/image/{uuid}?w={img_width}&h={img_height}")
        result.raise_for_status()

        with open(f"{save_path}/{uuid}.jpg", "wb") as f:
            f.write(result.content)

    except Exception as e:
        print(f"Failed to download image {uuid}: {e}")


def remove_image(uuid):
    full_filename = os.path.join(save_path, f"{uuid}.jpg")
    if os.path.isfile(full_filename):
        os.remove(full_filename)



def update():
    files = get_file_list()
    images = get_image_list()

    if not images:
        return

    for uuid in files: 
        if uuid not in images:
            remove_image(uuid)

    for uuid in images:
        if uuid not in files: 
            save_image(uuid)


while True:
    try:
        update()
    
    except Exception as e:
        print(f"Failed to update: {e}")

    time.sleep(refresh_time_s)