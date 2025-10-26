from fastapi import FastAPI, Response
from typing import Union
import uvicorn

import requests
from datetime import datetime

from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageOps
from io import BytesIO

from constants import api_key, api_endpoint, album_id, data_path_outside_docker

headers = {
    "x-api-key": api_key
}

def resize_to_1080p(image: Image.Image, target_size: (int, int)) -> Image.Image:
    """
    Resize an image to 1920x1080 while preserving aspect ratio
    and adding black bars as needed.
    """
    
    background = Image.new("RGB", target_size, (0, 0, 0))
    image = ImageOps.contain(image, target_size)
    x = (target_size[0] - image.width) // 2
    y = (target_size[1] - image.height) // 2
    background.paste(image, (x, y))
    return background


def add_text_with_vignette(image: Image.Image, text_lines: list[str]) -> Image.Image:
    """
    Adds lines of text in the bottom-left corner with a soft vignette background.
    The vignette scales according to the text size.
    """

    text_lines = list(filter(None, text_lines))

    if not text_lines:
        return image

    font_size = 30
    padding = 30
    line_spacing = 10

    image = image.convert("RGBA")

    try:
        font = ImageFont.truetype("Ubuntu-R.ttf", font_size)  # Adjust font path/size as needed
    except:
        font = ImageFont.load_default(size=font_size)

    line_sizes = [font.getbbox(line) for line in text_lines]
    widths = [bbox[2] - bbox[0] for bbox in line_sizes]
    heights = [bbox[3] - bbox[1] for bbox in line_sizes]

    text_width = max(widths)
    text_height = sum(heights) + (len(text_lines) - 1) * line_spacing

    vignette_width = text_width + 2 * padding
    vignette_height = text_height + 2 * padding

    vignette = Image.new("L", image.size, 0)
    ImageDraw.Draw(vignette).rectangle((0, image.height - vignette_height, vignette_width, image.height), fill=180) # Draw white rectangle
    vignette = vignette.filter(ImageFilter.GaussianBlur(50))  

    image.paste((0, 0, 0, 255), (0,0), vignette) # paste color black using vignette as mask

    draw = ImageDraw.Draw(image)
    current_y = image.height - text_height - padding
    for i, line in enumerate(text_lines):
        draw.text((padding, current_y), line, font=font, fill=(255, 255, 255, 255))
        current_y += heights[i] + line_spacing

    return image.convert("RGB")





app = FastAPI()

@app.get("/images")
def get_images():
    result = requests.get(url=f"{api_endpoint}/albums/{album_id}", headers=headers)
    result.raise_for_status()
    return {"uuid": [asset["id"] for asset in result.json()["assets"] if asset["type"] == "IMAGE"]}


@app.get("/image/{image_uuid}")
def read_item(image_uuid: str, w: Union[str, None] = None, h: Union[str, None] = None):


    # get image data
    result = requests.get(url=f"{api_endpoint}/assets/{image_uuid}", headers=headers)
    result.raise_for_status()
    asset = result.json()

    # open image
    result = requests.get(url=f"{api_endpoint}/assets/{image_uuid}/thumbnail?size=preview", headers=headers)
    result.raise_for_status()
    image = Image.open(BytesIO(result.content))

    #image = Image.open(f"{data_path_outside_docker}{asset["originalPath"]}")

    # process image
    resolution = (int(w) if w else 1920, int(h) if h else 1080)
    image = resize_to_1080p(image, resolution) 

    date = datetime.strptime(asset["localDateTime"][:10], '%Y-%m-%d').strftime('%d/%m/%Y')
    loc = f'{asset["exifInfo"]["city"]}, {asset["exifInfo"]["state"]}, {asset["exifInfo"]["country"]}'
    image = add_text_with_vignette(image, [date, loc])
    #image.show()

    buf = BytesIO()
    image.save(buf, format="JPEG", quality=90)
    buf.seek(0)

    return Response(content=buf.getvalue(), media_type="image/jpeg")


if __name__ == "__main__":
    uvicorn.run("proxy:app", host="127.0.0.1", port=5000, log_level="info") 